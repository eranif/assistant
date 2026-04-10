#include "assistant/chat_completions_response_parser.hpp"

#include <sstream>

#include "assistant/helpers.hpp"
#include "assistant/logger.hpp"

namespace assistant::chat_completions {

void ResponseParser::Parse(const std::string& text,
                           std::function<void(ParseResult)> cb) {
  AppendText(text);

  while (true) {
    auto line_opt = PopLine();
    if (!line_opt.has_value()) {
      cb(std::move(ParseResult{.need_more_data = true}));
      return;
    }

    std::string line = line_opt.value();

    // Handle SSE format: "data: {...}"
    if (line.find("data: ") != 0) {
      // Skip non-data lines (e.g., "event: message", empty lines)
      continue;
    }

    std::string data_content = line.substr(6);  // Remove "data: " prefix

    // Check for stream end marker
    if (data_content == "[DONE]") {
      cb(std::move(ParseResult{.is_done = true}));
      Reset();
      return;
    }

    // Try to parse as JSON
    auto json_opt = TryJson(data_content);
    if (!json_opt.has_value()) {
      // Invalid JSON, skip this line
      continue;
    }

    auto result = ProcessChunk(json_opt.value());
    cb(std::move(result));

    if (result.is_done) {
      Reset();
      return;
    }
  }
}

std::optional<std::string> ResponseParser::PopLine() {
  std::string current_line;
  enum class State { kStart, kCollect } state = State::kStart;

  for (size_t pos = 0; pos < m_content.size(); ++pos) {
    char c = m_content[pos];
    switch (state) {
      case State::kStart:
        if (c != '\n') {
          current_line += c;
          state = State::kCollect;
        } else {
          // Skip leading newlines
        }
        break;
      case State::kCollect:
        if (c == '\n') {
          state = State::kStart;
          m_content.erase(0, pos + 1);  // Consume the \n as well
          return current_line;
        } else {
          current_line += c;
        }
        break;
    }
  }
  return std::nullopt;
}

void ResponseParser::AppendText(const std::string& text) {
  m_content.append(text);
}

std::optional<json> ResponseParser::TryJson(std::string_view text) {
  try {
    auto trimmed = assistant::trim(text);
    auto _json = json::parse(std::string(trimmed));
    return _json;
  } catch (...) {
    return std::nullopt;
  }
}

ParseResult ResponseParser::ProcessChunk(const json& data) {
  ParseResult result;

  try {
    // Extract choices array
    if (!data.contains("choices") || !data["choices"].is_array() ||
        data["choices"].empty()) {
      return result;
    }

    const auto& choice = data["choices"][0];

    // Check if we have a delta object (streaming response)
    if (choice.contains("delta") && choice["delta"].is_object()) {
      const auto& delta = choice["delta"];

      // Extract content if present
      if (delta.contains("content") && delta["content"].is_string()) {
        result.content = delta["content"].get<std::string>();
      }

      // Extract tool_calls if present (streaming tool calls)
      if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
        for (const auto& tool_call : delta["tool_calls"]) {
          if (!tool_call.is_object()) {
            continue;
          }

          // Get tool call index
          int index = -1;
          if (tool_call.contains("index") && tool_call["index"].is_number()) {
            index = tool_call["index"].get<int>();
          }

          if (index < 0) {
            continue;
          }

          // Ensure we have a ToolCall for this index
          if (m_tool_calls.find(index) == m_tool_calls.end()) {
            m_tool_calls[index] = ToolCall();
          }

          auto& tc = m_tool_calls[index];

          // Extract function details
          if (tool_call.contains("id") && tool_call["id"].is_string()) {
            tc.id = tool_call["id"].get<std::string>();
          }

          if (tool_call.contains("type") && tool_call["type"].is_string()) {
            tc.type = tool_call["type"].get<std::string>();
          }

          // Extract function object
          if (tool_call.contains("function") &&
              tool_call["function"].is_object()) {
            const auto& func = tool_call["function"];

            if (func.contains("name") && func["name"].is_string()) {
              tc.name = func["name"].get<std::string>();
            }

            // For arguments, handle both streaming (partial_json) and complete
            if (func.contains("arguments")) {
              if (func["arguments"].is_string()) {
                // Complete arguments in this chunk
                tc.arguments_json = func["arguments"].get<std::string>();
              } else if (func.contains("partial_json") &&
                         func["partial_json"].is_string()) {
                // Streaming partial JSON (accumulate)
                tc.arguments_json += func["partial_json"].get<std::string>();
              }
            }
          }
        }
      }
    }

    // Check for finish_reason (indicates completion)
    if (choice.contains("finish_reason")) {
      auto fr = choice["finish_reason"];
      if (!fr.is_null() && fr.is_string()) {
        std::string finish_reason_str = fr.get<std::string>();

        if (finish_reason_str == "stop") {
          result.finish_reason = FinishReason::stop;
          result.is_done = true;
        } else if (finish_reason_str == "length") {
          result.finish_reason = FinishReason::length;
          result.is_done = true;
        } else if (finish_reason_str == "tool_calls") {
          result.finish_reason = FinishReason::tool_calls;
          result.is_done = true;

          // Add accumulated tool_calls to result
          for (const auto& [idx, tc] : m_tool_calls) {
            result.tool_calls.push_back(tc);
          }
        } else if (finish_reason_str == "content_filter") {
          result.finish_reason = FinishReason::content_filter;
          result.is_done = true;
        } else if (finish_reason_str == "function_call") {
          result.finish_reason = FinishReason::function_call;
          result.is_done = true;

          // Add accumulated tool_calls to result
          for (const auto& [idx, tc] : m_tool_calls) {
            result.tool_calls.push_back(tc);
          }
        }
      }
    }

    // Extract usage if present
    if (data.contains("usage") && data["usage"].is_object()) {
      const auto& usage_obj = data["usage"];
      Usage usage;

      if (usage_obj.contains("prompt_tokens") &&
          usage_obj["prompt_tokens"].is_number()) {
        usage.input_tokens = usage_obj["prompt_tokens"].get<int>();
      }

      if (usage_obj.contains("completion_tokens") &&
          usage_obj["completion_tokens"].is_number()) {
        usage.output_tokens = usage_obj["completion_tokens"].get<int>();
      }

      // Handle cache tokens if present (e.g., Claude models via OpenAI
      // compatibility layer)
      if (usage_obj.contains("cache_creation_input_tokens") &&
          usage_obj["cache_creation_input_tokens"].is_number()) {
        usage.cache_creation_input_tokens =
            usage_obj["cache_creation_input_tokens"].get<int>();
      }

      if (usage_obj.contains("cache_read_input_tokens") &&
          usage_obj["cache_read_input_tokens"].is_number()) {
        usage.cache_read_input_tokens =
            usage_obj["cache_read_input_tokens"].get<int>();
      }

      result.usage = usage;
    }

  } catch (const std::exception& e) {
    OLOG(LogLevel::kDebug)
        << "Error processing chunk in chat completions parser: " << e.what();
  } catch (...) {
    OLOG(LogLevel::kDebug)
        << "Unknown error processing chunk in chat completions parser";
  }

  return result;
}

std::optional<std::string> ResponseParser::GetErrorMessage(
    const std::string& response) {
  try {
    auto j = json::parse(response);

    // Standard OpenAI error format
    if (j.contains("error") && j["error"].is_object()) {
      const auto& error = j["error"];
      if (error.contains("message") && error["message"].is_string()) {
        return error["message"].get<std::string>();
      }
    }

    return std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace assistant::chat_completions
