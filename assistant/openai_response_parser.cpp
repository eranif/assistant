#include "assistant/openai_response_parser.hpp"

#include <sstream>

#include "assistant/helpers.hpp"
#include "assistant/logger.hpp"

namespace assistant {

void OpenAIResponseParser::Parse(const std::string& data, OnParseCallback cb) {
  // Append new data to buffer
  m_line_buffer += data;

  // Process complete lines
  size_t pos = 0;
  while ((pos = m_line_buffer.find('\n')) != std::string::npos) {
    std::string line = m_line_buffer.substr(0, pos);
    m_line_buffer = m_line_buffer.substr(pos + 1);

    // Trim the line
    line = assistant::trim(line);

    // Skip empty lines
    if (line.empty()) {
      continue;
    }

    ParseLine(line, cb);
  }

  // If buffer is getting too large without finding a newline, might be
  // incomplete
  if (m_line_buffer.size() > 10000) {
    OLOG(LogLevel::kWarning)
        << "OpenAI response parser: line buffer exceeded 10KB without "
           "finding newline";
    cb(ParseResult{.need_more_data = true});
  }
}

void OpenAIResponseParser::ParseLine(const std::string& line,
                                     OnParseCallback cb) {
  // OpenAI Responses API SSE format uses "event:" and "data:" lines
  OLOG(LogLevel::kDebug) << line;
  if (line.find("event: ") == 0) {
    m_current_event = line.substr(7);
    return;
  }

  if (line.find("data: ") != 0) {
    return;
  }

  std::string data_content = line.substr(6);  // Remove "data: " prefix

  // Check for stream end marker
  if (data_content == "[DONE]") {
    cb(ParseResult{.is_done = true});
    return;
  }

  try {
    auto json_obj = json::parse(data_content);
    auto event_type = json_obj["type"].get<std::string>();

    if (event_type == "response.failed") {
      auto reason = ExtractError(json_obj);
      ParseResult result{.is_done = true,
                         .is_error = true,
                         .error_message = reason.value_or("")};
      if (json_obj.contains("response") && json_obj["response"].is_object()) {
        result.usage = ExtractUsage(json_obj["response"]);
      }
      cb(result);
      return;
    }

    if (event_type == "response.completed") {
      ParseResult result{.is_done = true};
      if (json_obj.contains("response") && json_obj["response"].is_object()) {
        result.usage = ExtractUsage(json_obj["response"]);
        result.content = ExtractContent(json_obj["response"]).value_or("");
        // Extract content from top-level output if not in response
        if (result.content.empty()) {
          result.content = ExtractContent(json_obj).value_or("");
        }
      }
      result.finish_reason = ExtractFinishReason(json_obj);
      cb(result);
      return;
    }

    if (event_type == "response.incomplete") {
      ParseResult result{.is_done = true, .is_error = true};
      if (json_obj.contains("response") && json_obj["response"].is_object()) {
        result.usage = ExtractUsage(json_obj["response"]);
      }
      cb(result);
      return;
    }

    if (event_type == "response.output_text.delta") {
      auto text = json_obj["delta"].get<std::string>();
      cb(ParseResult{.content = text});
      return;
    }

    if (event_type == "response.output_item.done" &&
        json_obj["item"]["type"].get<std::string>() == "function_call") {
      ParseResult tool_call;
      tool_call.is_tool_call = true;
      tool_call.tool_call_id = json_obj["item"]["call_id"].get<std::string>();
      tool_call.tool_name = json_obj["item"]["name"].get<std::string>();
      tool_call.tool_args =
          json::parse(json_obj["item"]["arguments"].get<std::string>());
      cb(tool_call);
      return;
    }
  } catch (const json::parse_error& e) {
    OLOG(LogLevel::kError) << "OpenAI response parser: JSON parse error: "
                           << e.what();
    cb(ParseResult{
        .is_done = true,
        .is_error = true,
        .error_message = std::string("JSON parse error: ") + e.what()});
  } catch (const std::exception& e) {
    OLOG(LogLevel::kError) << "OpenAI response parser: exception: " << e.what();
    cb(ParseResult{
        .is_done = true,
        .is_error = true,
        .error_message = std::string("Parser error: ") + e.what(),
    });
  }
}

std::optional<std::string> OpenAIResponseParser::ExtractContent(
    const json& json_obj) {
  try {
    // /v1/responses streaming: response.output_text.delta event
    // {"delta": "text", ...}
    if (json_obj.contains("delta") && json_obj["delta"].is_string()) {
      return json_obj["delta"].get<std::string>();
    }

    // /v1/responses completed: output[].content[].text
    if (json_obj.contains("output") && json_obj["output"].is_array()) {
      for (const auto& item : json_obj["output"]) {
        if (!item.contains("content") || !item["content"].is_array()) continue;
        for (const auto& part : item["content"]) {
          if (part.contains("text") && part["text"].is_string()) {
            return part["text"].get<std::string>();
          }
        }
      }
    }

    return std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<Usage> OpenAIResponseParser::ExtractUsage(const json& json_obj) {
  try {
    // "usage":{"input_tokens":199,"input_tokens_details":{"cached_tokens":0},
    // "output_tokens":15,"output_tokens_details":{"reasoning_tokens":0},"total_tokens":214}
    if (!json_obj.contains("usage") || !json_obj["usage"].is_object()) {
      return std::nullopt;
    }

    const auto& usage_obj = json_obj["usage"];
    Usage usage;

    // /v1/responses format: input_tokens / output_tokens
    if (usage_obj.contains("input_tokens")) {
      usage.input_tokens = usage_obj["input_tokens"].get<uint32_t>();
      // subtract cached tokens (if any).
      uint32_t cached_tokens{0};
      if (usage_obj.contains("input_tokens_details") &&
          usage_obj["input_tokens_details"].is_object() &&
          usage_obj["input_tokens_details"].contains("cached_tokens") &&
          usage_obj["input_tokens_details"]["cached_tokens"].is_number()) {
        cached_tokens =
            usage_obj["input_tokens_details"]["cached_tokens"].get<uint32_t>();
      }
      usage.input_tokens -= cached_tokens;
      usage.cache_read_input_tokens = cached_tokens;
    }

    if (usage_obj.contains("output_tokens")) {
      usage.output_tokens = usage_obj["output_tokens"].get<uint32_t>();
    }
    return usage;

  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> OpenAIResponseParser::ExtractFinishReason(
    const json& json_obj) {
  try {
    // /v1/responses format: top-level "status" field
    if (json_obj.contains("status") && json_obj["status"].is_string()) {
      return json_obj["status"].get<std::string>();
    }
    return std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::vector<json>> OpenAIResponseParser::ExtractOutput(
    const json& json_obj) {
  try {
    // /v1/responses format: top-level "output" field
    if (json_obj.contains("output") && json_obj["output"].is_array()) {
      std::vector<json> objs = json_obj["output"].get<std::vector<json>>();
      return objs;
    }
    return std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> OpenAIResponseParser::ExtractError(
    const json& json_obj) {
  try {
    // OpenAI error format: {"error":{"message":"error
    // text","type":"error_type"}}
    if (!json_obj.contains("error")) {
      return std::nullopt;
    }

    const auto& error = json_obj["error"];
    if (error.is_object() && error.contains("message")) {
      return error["message"].get<std::string>();
    }

    return std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace assistant
