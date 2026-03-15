#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "assistant/common.hpp"
#include "assistant/common/json.hpp"

namespace assistant {

using json = nlohmann::ordered_json;

/// OpenAI streaming response parser for Server-Sent Events (SSE) format.
/// OpenAI returns streaming responses in the following format:
/// data:
/// {"id":"chatcmpl-123","choices":[{"delta":{"content":"Hello"},"index":0}]}
/// data: [DONE]
class OpenAIResponseParser {
 public:
  struct ParseResult {
    std::string content;
    bool is_done{false};
    bool need_more_data{false};
    std::optional<std::string> error_message;
    std::optional<Usage> usage;
    std::optional<std::string> finish_reason;

    inline bool HasValue() const { return !content.empty(); }
    inline bool IsDone() const { return is_done; }
    inline bool NeedMoreData() const { return need_more_data; }
    inline bool HasError() const { return error_message.has_value(); }
  };

  using OnParseCallback = std::function<void(ParseResult)>;

  OpenAIResponseParser() = default;
  ~OpenAIResponseParser() = default;

  /// Parse OpenAI SSE response stream.
  /// @param data The raw SSE data chunk
  /// @param cb Callback function to invoke for each parsed message
  void Parse(const std::string& data, OnParseCallback cb);

 private:
  /// Parse a single SSE event line
  /// @param line The SSE line to parse
  /// @param cb Callback function to invoke for parsed content
  void ParseLine(const std::string& line, OnParseCallback cb);

  /// Extract content from OpenAI delta response
  /// @param json_obj The parsed JSON object
  /// @return The content string if available
  std::optional<std::string> ExtractContent(const json& json_obj);

  /// Extract usage information from OpenAI response
  /// @param json_obj The parsed JSON object
  /// @return Usage information if available
  std::optional<Usage> ExtractUsage(const json& json_obj);

  /// Extract finish reason from OpenAI response
  /// @param json_obj The parsed JSON object
  /// @return Finish reason if available
  std::optional<std::string> ExtractFinishReason(const json& json_obj);

  /// Extract error message from OpenAI response
  /// @param json_obj The parsed JSON object
  /// @return Error message if available
  std::optional<std::string> ExtractError(const json& json_obj);

  /// Buffer for incomplete lines
  std::string m_line_buffer;
};

}  // namespace assistant
