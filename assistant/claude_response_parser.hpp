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

struct ParserResult {
  bool is_done{false};
  bool need_more_data{false};
  json tool_use{nullptr};
  std::optional<ContentType> content_type{std::nullopt};
  std::string text;
};

/// A state-ful claude response parser.
class ResponseParser {
 public:
  ResponseParser() = default;
  ~ResponseParser() = default;
  ParserResult Parse(const std::string& message);

 private:
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
