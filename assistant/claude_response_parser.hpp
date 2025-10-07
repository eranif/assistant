#pragma once

#include <string>
#include <vector>

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

struct EventMessage {
  Event event;
  std::string data;
};

struct ParseResult {
  bool is_done{false};
  bool need_more_data{false};
  std::optional<ContentType> content_type{std::nullopt};
  std::string content;

  inline bool HasValue() const { return content_type.has_value(); }
  inline bool NeedMoreData() const { return need_more_data; }
  inline bool IsDone() const { return is_done; }
};

/// A state-ful claude response parser.
class ResponseParser {
 public:
  ResponseParser() = default;
  ~ResponseParser() = default;
  void Parse(const std::string& text, std::function<void(ParseResult)> cb);

 private:
  void AppendText(const std::string& text);

  /// This function might throw.
  std::optional<EventMessage> NextMessage();
  /// This function might throw.
  ContentType GetContentBlock(const EventMessage& event_message);
  /// This function might throw.
  std::string GetContentBlockDeltaContent(const EventMessage& event_message);
  std::string m_content;
  std::vector<std::string> m_lines;
  ParserState m_state{ParserState::initial};
  std::string m_tool_use_json;
};

}  // namespace assistant::claude
