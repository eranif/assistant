#pragma once

#include <string>
#include <vector>

#include "assistant/client/client_base.hpp"
#include "assistant/common/json.hpp"

namespace assistant::chat_completions {

using json = nlohmann::ordered_json;

enum class FinishReason {
  stop,
  length,
  tool_calls,
  content_filter,
  function_call,
  error,
};

struct ToolCall {
  std::string id;
  std::string type;  // "function"
  std::string name;
  std::string arguments_json;

  inline void Reset() {
    id.clear();
    type.clear();
    name.clear();
    arguments_json.clear();
  }
};

inline std::ostream& operator<<(std::ostream& os, const ToolCall& tc) {
  os << "ToolCall{.id=" << tc.id << ", .type=" << tc.type
     << ", .name=" << tc.name << ", .arguments_json=" << tc.arguments_json << "}";
  return os;
}

struct ParseResult {
  bool is_done{false};
  bool need_more_data{false};
  std::string content;
  std::optional<FinishReason> finish_reason{std::nullopt};
  std::vector<ToolCall> tool_calls;
  std::optional<Usage> usage{std::nullopt};

  inline bool NeedMoreData() const { return need_more_data; }
  inline bool IsDone() const { return is_done; }
  inline bool IsToolCall() const { return !tool_calls.empty(); }
  inline bool HasContent() const { return !content.empty(); }
  inline const std::optional<Usage> GetUsage() const { return usage; }

  inline Reason GetReason() const {
    if (IsDone()) {
      if (finish_reason.value_or(FinishReason::stop) == FinishReason::length) {
        OLOG(LogLevel::kWarning)
            << "We exceeded the requested max_tokens or the model's maximum";
      }
      return Reason::kDone;
    } else {
      return Reason::kPartialResult;
    }
  }
};

inline std::ostream& operator<<(std::ostream& os, const ParseResult& res) {
  os << "ParseResult{.is_done=" << res.is_done
     << ", .need_more_data=" << res.need_more_data
     << ", .content=" << res.content
     << ", .tool_calls.size()=" << res.tool_calls.size() << "}";
  return os;
}

/// A stateful Chat Completions response parser for SSE format.
class ResponseParser {
 public:
  ResponseParser() = default;
  ~ResponseParser() = default;

  void Parse(const std::string& text, std::function<void(ParseResult)> cb);

  inline void Reset() {
    m_content.clear();
    m_tool_calls.clear();
  }

  static std::optional<std::string> GetErrorMessage(const std::string& response);

 private:
  void AppendText(const std::string& text);
  std::optional<std::string> PopLine();
  std::optional<json> TryJson(std::string_view text);

  ParseResult ProcessChunk(const json& data);

  std::string m_content;
  std::map<int, ToolCall> m_tool_calls;  // index -> ToolCall (for accumulating streaming tool calls)
};

}  // namespace assistant::chat_completions
