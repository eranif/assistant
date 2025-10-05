#include "assistant/claude_response_parser.hpp"

#include "assistant/common/magic_enum.hpp"
#include "assistant/helpers.hpp"
#include "assistant/logger.hpp"

namespace assistant::claude {

ParserResult ResponseParser::Parse(const std::string& message) {
  // Split the message into lines.
  m_content.append(message);
  auto res = split_into_lines(m_content);
  m_content = res.second;  // the remainder
  m_lines.insert(m_lines.end(), res.first.begin(), res.first.end());

  while (true) {
    auto event_message_opt = NextMessage();
    if (!event_message_opt.has_value()) {
      return ParserResult{.need_more_data = true};
    }

    auto event_message = event_message_opt.value();
    switch (m_state) {
      case ParserState::initial: {
        switch (event_message.event) {
          case Event::message_start:
          case Event::ping:
          case Event::message_delta:
            break;
          case Event::message_stop:
            return ParserResult{.is_done = true};
          case Event::content_block_start: {
            auto content_block_type = GetContentBlock(event_message);
            switch (content_block_type) {
              case ContentType::text:
                m_state = ParserState::collect_text;
                break;
              case ContentType::thinking:
                m_state = ParserState::collect_thinking;
                break;
              case ContentType::tool_use:
                m_state = ParserState::collect_tool_use_json;
                m_tool_use_json.clear();
                break;
            }
          } break;
          default: {
            // any other event type is invalid during the initial state.
            std::stringstream ss;
            ss << "Invalid message: "
               << magic_enum::enum_name<Event>(event_message.event);
            throw std::runtime_error(ss.str());
          } break;
        }
        break;
        case ParserState::collect_text:
          switch (event_message.event) {
            case Event::content_block_delta: {
              // data:
              // {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"
              // Francisco"}}
              std::string text = GetContentBlockDeltaContent(event_message);
              return ParserResult{.content_type = ContentType::text,
                                  .text = text};
            } break;
            case Event::content_block_stop:
              m_state = ParserState::initial;
              break;
            case Event::ping:
              break;
            default:
              OLOG(LogLevel::kWarning)
                  << "Received an unexpected event type while in state "
                     "collect_text: "
                  << magic_enum::enum_name<Event>(event_message.event);
              break;
          }
          break;
        case ParserState::collect_tool_use_json:
          // We collect all the JSON parts and return a complete JSON string.
          switch (event_message.event) {
            case Event::content_block_delta:
              m_tool_use_json.append(
                  GetContentBlockDeltaContent(event_message));
              break;
            case Event::content_block_stop: {
              auto result = ParserResult{.content_type = ContentType::tool_use,
                                         .text = m_tool_use_json};
              m_tool_use_json.clear();
              m_state = ParserState::initial;
              return result;
            } break;
            default:
              OLOG(LogLevel::kWarning)
                  << "Received an unexpected event type while in state: "
                     "collect_tool_use_json: "
                  << magic_enum::enum_name<Event>(event_message.event);
              break;
          }
          break;
        case ParserState::collect_thinking:
          switch (event_message.event) {
            case Event::content_block_delta: {
              // data: {"type": "content_block_delta", "index": 0, "delta":
              // {"type": "thinking_delta", "thinking": "\n2. 453 = 400 + 50 +
              // 3"}}
              // OR
              // data: {"type": "content_block_delta", "index": 0, "delta":
              // {"type": "signature_delta", "signature":
              // "EqQBCgIYAhIM1gbcDa9GJwZA2b3hGgxBdjrkzLoky3dl1pkiMOYds..."}}
              std::string text = GetContentBlockDeltaContent(event_message);
              return ParserResult{.content_type = ContentType::thinking,
                                  .text = text};
            } break;
            case Event::content_block_stop:
              m_state = ParserState::initial;
              break;
            case Event::ping:
              break;
            default:
              OLOG(LogLevel::kWarning)
                  << "Unexpected event type: "
                  << magic_enum::enum_name<Event>(event_message.event);
              break;
          }
          break;
      }
    }
  }
}

std::optional<EventMessage> ResponseParser::NextMessage() {
  if (m_lines.size() < 2) {
    return std::nullopt;
  }

  std::string event_str = m_lines.front();
  auto event_type_str = assistant::after_first(event_str, ":");
  if (event_type_str.empty()) {
    std::stringstream ss;
    ss << "Invalid input line. Line must start with 'event:'. Actual line is: '"
       << event_str << "'";
    throw std::runtime_error(ss.str());
  }

  auto event_type =
      magic_enum::enum_cast<Event>(assistant::trim(event_type_str));
  if (!event_type.has_value()) {
    std::stringstream ss;
    ss << "Invalid event type: " << event_type_str;
    throw std::runtime_error(ss.str());
  }

  m_lines.erase(m_lines.begin());
  std::string data_line = m_lines.front();
  m_lines.erase(m_lines.begin());

  data_line = assistant::after_first(data_line, ":");
  data_line = assistant::trim(data_line);

  EventMessage em{.event = event_type.value(), .data = data_line};
  return em;
}

std::string ResponseParser::GetContentBlockDeltaContent(
    const EventMessage& event_message) {
  // data:
  // {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":""}}
  // data:
  // {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"
  // Francisco"}}
  // data: {"type": "content_block_delta", "index": 0, "delta": {"type":
  // "thinking_delta", "thinking": "\n2. 453 = 400 + 50 + 3"}}
  // data: {"type": "content_block_delta", "index": 0, "delta": {"type":
  // "signature_delta", "signature":
  // "EqQBCgIYAhIM1gbcDa9GJwZA2b3hGgxBdjrkzLoky3dl1pkiMOYds..."}}
  auto j = json::parse(event_message.data);
  std::string type = j["delta"]["type"].get<std::string>();
  auto res = magic_enum::enum_cast<DeltaType>(type);
  if (!res.has_value()) {
    std::stringstream ss;
    ss << "Invalid 'delta' type: " << type;
    throw std::runtime_error(ss.str());
  }

  switch (res.value()) {
    case DeltaType::text_delta:
      return j["delta"]["text"].get<std::string>();
    case DeltaType::input_json_delta:
      return j["delta"]["partial_json"].get<std::string>();
    case DeltaType::thinking_delta:
      return j["delta"]["thinking"].get<std::string>();
    case DeltaType::signature_delta:
      // we don't care (for now) about the signature.
      return "";
  }
}

ContentType ResponseParser::GetContentBlock(const EventMessage& event_message) {
  // Check the type of the content
  // data:
  // {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}
  auto j = json::parse(event_message.data);
  // might throw here
  std::string type = j["content_block"]["type"].get<std::string>();
  auto res = magic_enum::enum_cast<ContentType>(type);
  if (!res.has_value()) {
    std::stringstream ss;
    ss << "Invalid 'content_block' type: " << type;
    throw std::runtime_error(ss.str());
  }
  return res.value();
}
}  // namespace assistant::claude
