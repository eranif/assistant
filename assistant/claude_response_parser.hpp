#pragma once

#include <string>
#include <vector>

#include "assistant/client_base.hpp"
#include "assistant/common/json.hpp"

namespace assistant::claude {

using nlohmann::json;

enum class Event {
  message_start,
  message_delta,
  message_stop,
  content_block_start,
  ping,
  content_block_delta,
  content_block_stop,
};

enum class ContentType {
  text,
  tool_use,
  thinking,
};

enum class DeltaType {
  input_json_delta,
  text_delta,
  thinking_delta,
  signature_delta,
};

enum ParserState {
  initial,
  collect_text,
  collect_tool_use_json,
  collect_thinking,
};

enum class StopReason {
  ///  the model reached a natural stopping point
  end_turn,
  /// we exceeded the requested max_tokens or the model's maximum
  max_tokens,
  /// one of your provided custom stop_sequences was generated
  stop_sequence,
  /// the model invoked one or more tools
  tool_use,
  /// we paused a long-running turn. You may provide the response
  pause_turn,
};

struct EventMessage {
  Event event;
  std::string data;
};

struct ToolCall {
  std::string name;
  std::string id;
  std::string json_str;
  inline void Reset() {
    name.clear();
    id.clear();
    json_str.clear();
  }
};

inline std::ostream& operator<<(std::ostream& os, const ToolCall& tc) {
  os << "ToolCall{.name=" << tc.name << ", .id=" << tc.id
     << ", .json_str=" << tc.json_str << "}";
  return os;
}

struct ParseResult {
  bool is_done{false};
  bool need_more_data{false};
  std::optional<ContentType> content_type{std::nullopt};
  std::string content;
  std::optional<StopReason> stop_reason{std::nullopt};
  ToolCall tool_call;

  inline bool HasValue() const { return content_type.has_value(); }
  inline bool NeedMoreData() const { return need_more_data; }
  inline bool IsDone() const { return is_done; }
  inline bool IsToolCall() const {
    return content_type.has_value() &&
           content_type.value() == ContentType::tool_use;
  }

  inline const std::string& GetToolName() const { return tool_call.name; }
  inline const std::string& GetToolId() const { return tool_call.id; }
  inline const std::string& GetToolJsonStr() const {
    return tool_call.json_str;
  }

  inline json GetToolJson() const {
    try {
      // Might be an empty string. use try/catch
      return json::parse(GetToolJsonStr());
    } catch (...) {
      // empty **not null** object
      return json({});
    }
  }

  inline bool IsThinking() const {
    return content_type.has_value() &&
           content_type.value() == ContentType::thinking;
  }
  inline Reason GetReason() const {
    if (IsDone()) {
      // Check the real reason.
      if (stop_reason.value_or(StopReason::end_turn) ==
          StopReason::max_tokens) {
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
     << ", .content=" << res.content << ", .tool_call=" << res.tool_call << "}";
  return os;
}

/// A state-ful claude response parser.
class ResponseParser {
 public:
  ResponseParser() = default;
  ~ResponseParser() = default;
  void Parse(const std::string& text, std::function<void(ParseResult)> cb);
  inline void Reset() {
    m_content.clear();
    m_lines.clear();
    m_state = ParserState::initial;
    m_tool_call.Reset();
  }

 private:
  void AppendText(const std::string& text);
  std::optional<json> TryJson(std::string_view text);

  /// This function might throw.
  std::optional<EventMessage> NextMessage();
  /// This function might throw.
  ContentType GetContentBlock(const EventMessage& event_message);
  /// This function might throw.
  std::string GetToolName(const EventMessage& event_message);
  /// This function might throw.
  std::string GetToolId(const EventMessage& event_message);
  std::optional<StopReason> GetStopReason(const EventMessage& event_message);

  /// This function might throw.
  std::string GetContentBlockDeltaContent(const EventMessage& event_message);
  std::string m_content;
  std::vector<std::string> m_lines;
  ParserState m_state{ParserState::initial};
  ToolCall m_tool_call;
};

}  // namespace assistant::claude
