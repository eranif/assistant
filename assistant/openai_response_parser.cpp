#include "assistant/openai_response_parser.hpp"

#include <sstream>

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

    // Remove carriage return if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

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
  // OpenAI SSE format: "data: {json}" or "data: [DONE]"
  if (line.find("data: ") != 0) {
    // Not a data line, skip
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

    // Check for errors
    auto error = ExtractError(json_obj);
    if (error.has_value()) {
      cb(ParseResult{.is_done = true, .error_message = error.value()});
      return;
    }

    // Extract content from delta
    auto content = ExtractContent(json_obj);
    auto usage = ExtractUsage(json_obj);
    auto finish_reason = ExtractFinishReason(json_obj);

    // Build result
    ParseResult result;
    if (content.has_value()) {
      result.content = content.value();
    }
    result.usage = usage;
    result.finish_reason = finish_reason;

    // Check if this is the final message
    if (finish_reason.has_value()) {
      result.is_done = true;
    }

    // Only invoke callback if we have something to report
    if (result.HasValue() || result.usage.has_value() ||
        result.finish_reason.has_value()) {
      cb(result);
    }

  } catch (const json::parse_error& e) {
    OLOG(LogLevel::kError) << "OpenAI response parser: JSON parse error: "
                           << e.what();
    cb(ParseResult{
        .is_done = true,
        .error_message = std::string("JSON parse error: ") + e.what()});
  } catch (const std::exception& e) {
    OLOG(LogLevel::kError) << "OpenAI response parser: exception: " << e.what();
    cb(ParseResult{
        .is_done = true,
        .error_message = std::string("Parser error: ") + e.what(),
    });
  }
}

std::optional<std::string> OpenAIResponseParser::ExtractContent(
    const json& json_obj) {
  try {
    // OpenAI streaming format:
    // {"choices":[{"delta":{"content":"text"},"index":0}]}
    // or for non-streaming:
    // {"choices":[{"message":{"content":"text"},"index":0}]}

    if (!json_obj.contains("choices") || !json_obj["choices"].is_array() ||
        json_obj["choices"].empty()) {
      return std::nullopt;
    }

    const auto& choice = json_obj["choices"][0];

    // Try delta.content first (streaming)
    if (choice.contains("delta") && choice["delta"].is_object()) {
      const auto& delta = choice["delta"];
      if (delta.contains("content") && delta["content"].is_string()) {
        return delta["content"].get<std::string>();
      }

      // Check for tool calls in delta
      if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
        // Tool calls handling can be added here if needed
        OLOG(LogLevel::kDebug) << "OpenAI response contains tool_calls";
      }
    }

    // Try message.content (non-streaming)
    if (choice.contains("message") && choice["message"].is_object()) {
      const auto& message = choice["message"];
      if (message.contains("content") && message["content"].is_string()) {
        return message["content"].get<std::string>();
      }
    }

    return std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<Usage> OpenAIResponseParser::ExtractUsage(const json& json_obj) {
  try {
    // OpenAI format:
    // {"usage":{"prompt_tokens":10,"completion_tokens":20,"total_tokens":30}}
    if (!json_obj.contains("usage") || !json_obj["usage"].is_object()) {
      return std::nullopt;
    }

    const auto& usage_obj = json_obj["usage"];
    Usage usage;

    if (usage_obj.contains("prompt_tokens")) {
      usage.input_tokens = usage_obj["prompt_tokens"].get<size_t>();
    }

    if (usage_obj.contains("completion_tokens")) {
      usage.output_tokens = usage_obj["completion_tokens"].get<size_t>();
    }

    if (usage_obj.contains("total_tokens")) {
      // OpenAI provides total_tokens directly
      // Verify it matches our calculation
      size_t total = usage_obj["total_tokens"].get<size_t>();
      if (total != usage.input_tokens + usage.output_tokens) {
        OLOG(LogLevel::kWarning)
            << "OpenAI total_tokens mismatch: " << total << " vs "
            << (usage.input_tokens + usage.output_tokens);
      }
    }

    return usage;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> OpenAIResponseParser::ExtractFinishReason(
    const json& json_obj) {
  try {
    // OpenAI format: {"choices":[{"finish_reason":"stop"}]}
    if (!json_obj.contains("choices") || !json_obj["choices"].is_array() ||
        json_obj["choices"].empty()) {
      return std::nullopt;
    }

    const auto& choice = json_obj["choices"][0];
    if (choice.contains("finish_reason") &&
        !choice["finish_reason"].is_null()) {
      return choice["finish_reason"].get<std::string>();
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
