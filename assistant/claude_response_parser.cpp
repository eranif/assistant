#include "assistant/claude_response_parser.hpp"

#include "assistant/common/magic_enum.hpp"
#include "assistant/helpers.hpp"
#include "assistant/logger.hpp"

namespace assistant::claude {

void ResponseParser::Parse(const std::string& text,
                           std::function<void(ParseResult)> cb) {
  AppendText(text);

  while (true) {
    auto event_message_opt = NextMessage();
    if (!event_message_opt.has_value()) {
      cb(std::move(ParseResult{.need_more_data = true}));
      return;
    }

    OLOG(LogLevel::kDebug) << "Processing event: "
                           << magic_enum::enum_name<Event>(
                                  event_message_opt.value().event);
    OLOG(LogLevel::kDebug) << "Data: " << event_message_opt.value().data;

    auto event_message = event_message_opt.value();
    switch (m_state) {
      case ParserState::initial: {
        switch (event_message.event) {
          case Event::message_start:
          case Event::ping:
            break;
          case Event::message_delta:
            cb(std::move(ParseResult{.is_done = false,
                                     .usage = GetUsage(event_message)}));
            break;
          case Event::message_stop:
            cb(std::move(
                ParseResult{.is_done = true,
                            .stop_reason = GetStopReason(event_message),
                            .usage = GetUsage(event_message)}));
            Reset();
            return;
          case Event::error:
            cb(std::move(ParseResult{
                .is_done = true,
                .content = GetErrorMessage(event_message.data).value_or(""),
                .stop_reason = StopReason::error,
            }));
            Reset();
            return;
          case Event::content_block_start: {
            auto content_block_type = GetContentBlock(event_message);
            switch (content_block_type) {
              case ContentType::text:
                m_state = ParserState::collect_text;
                break;
              case ContentType::thinking:
                m_state = ParserState::collect_thinking;
                break;
              case ContentType::tool_use: {
                // get the toolname
                m_tool_call.Reset();
                m_tool_call.name = GetToolName(event_message);  // might throw
                m_tool_call.id = GetToolId(event_message);      // might throw
                m_state = ParserState::collect_tool_use_json;
              } break;
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
            case Event::error:
              cb(std::move(ParseResult{
                  .is_done = true,
                  .content = GetErrorMessage(event_message.data).value_or(""),
                  .stop_reason = StopReason::error,
              }));
              Reset();
              return;
            case Event::message_stop:
              cb(std::move(
                  ParseResult{.is_done = true,
                              .stop_reason = GetStopReason(event_message),
                              .usage = GetUsage(event_message)}));
              Reset();
              return;
            case Event::content_block_delta: {
              // data:
              // {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"
              // Francisco"}}
              std::string text = GetContentBlockDeltaContent(event_message);
              cb(std::move(ParseResult{.content_type = ContentType::text,
                                       .content = text}));
            } break;
            case Event::content_block_stop:
              m_state = ParserState::initial;
              break;
            case Event::content_block_start:
            case Event::message_delta:
            case Event::message_start:
            case Event::ping:
              break;
          }
          break;
        case ParserState::collect_tool_use_json:
          // We collect all the JSON parts and return a complete JSON string.
          switch (event_message.event) {
            case Event::message_delta:
            case Event::ping:
              break;
            case Event::content_block_delta:
              m_tool_call.json_str.append(
                  GetContentBlockDeltaContent(event_message));
              break;
            case Event::content_block_stop: {
              cb(std::move(ParseResult{.content_type = ContentType::tool_use,
                                       .tool_call = m_tool_call}));
              m_tool_call.Reset();
              m_state = ParserState::initial;
            } break;
            case Event::error:
              cb(std::move(ParseResult{
                  .is_done = true,
                  .content = GetErrorMessage(event_message.data).value_or(""),
                  .stop_reason = StopReason::error,
              }));
              Reset();
              return;
            case Event::message_stop:
              cb(std::move(
                  ParseResult{.is_done = true,
                              .stop_reason = GetStopReason(event_message),
                              .usage = GetUsage(event_message)}));
              Reset();
              return;
            case Event::content_block_start:
            case Event::message_start:
              break;
          }
          break;
        case ParserState::collect_thinking:
          switch (event_message.event) {
            case Event::content_block_delta: {
              std::string text = GetContentBlockDeltaContent(event_message);
              cb(std::move(ParseResult{.content_type = ContentType::thinking,
                                       .content = text}));
            } break;
            case Event::content_block_stop:
              m_state = ParserState::initial;
              break;
            case Event::message_stop:
              cb(std::move(
                  ParseResult{.is_done = true,
                              .stop_reason = GetStopReason(event_message),
                              .usage = GetUsage(event_message)}));
              Reset();
              return;
            case Event::error:
              cb(std::move(ParseResult{
                  .is_done = true,
                  .content = GetErrorMessage(event_message.data).value_or(""),
                  .stop_reason = StopReason::error,
              }));
              Reset();
              return;
            case Event::ping:
            case Event::content_block_start:
            case Event::message_start:
            case Event::message_delta:
              break;
          }
          break;
      }
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
          // found "\n", skip it.
        }
        break;
      case State::kCollect:
        if (c == '\n') {
          state = State::kStart;
          m_content.erase(0, pos + 1);  // consume the \n as well.
          return current_line;
        } else {
          current_line += c;
        }
        break;
    }
  }
  return std::nullopt;
}

void ResponseParser::PushLineFront(const std::string& line) {
  std::string line_to_restore = line;
  if (!line_to_restore.ends_with("\n")) {
    line_to_restore += "\n";
  }

  auto new_content = line_to_restore + m_content;
  m_content.swap(new_content);
}

std::optional<EventMessage> ResponseParser::NextMessage() {
  auto event_str_opt = PopLine();
  if (!event_str_opt.has_value()) {
    return std::nullopt;
  }

  auto data_str_opt = PopLine();
  if (!data_str_opt.has_value()) {
    PushLineFront(event_str_opt.value());
    return std::nullopt;
  }

  std::string event_str = event_str_opt.value();
  std::string data_str = data_str_opt.value();

  auto event_type_str = assistant::after_first(event_str, ":");
  if (event_type_str.empty()) {
    std::stringstream ss;
    ss << "Invalid input line. Line must start with 'event:'. Actual line "
          "is: '"
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

  // Make sure the data_line is a complete json.
  data_str = assistant::after_first(data_str, ":");
  data_str = assistant::trim(data_str);

  auto result = assistant::try_read_jsons_from_string(data_str);
  if (result.first.empty()) {
    std::cout << "\n! Could not parse:\n"
              << data_str << "\ninto a json." << std::endl;
    return std::nullopt;
  }

  EventMessage em{.event = event_type.value(), .data = data_str};
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
  return "";
}

std::optional<StopReason> ResponseParser::GetStopReason(
    const EventMessage& event_message) {
  auto j = json::parse(event_message.data);
  try {
    if (j["delta"]["stop_reason"].is_null()) {
      return std::nullopt;
    }
    std::string stop_reason = j["delta"]["stop_reason"].get<std::string>();
    return magic_enum::enum_cast<StopReason>(stop_reason);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<Usage> ResponseParser::GetUsage(
    const EventMessage& event_message) {
  auto j = json::parse(event_message.data);
  try {
    if (!j.contains("usage") || !j["usage"].is_object()) {
      return std::nullopt;
    }
    return Usage::FromClaudeJson(j["usage"]);

  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> ResponseParser::GetErrorMessage(
    const std::string& event_message) {
  // data example incase of an error:
  // {"type":"error","error":{"details":null,"type":"overloaded_error","message":"Overloaded"},"request_id":"req_011CXnsFdbuV3b51g7bRNngu"
  // }
  auto j = json::parse(event_message);
  try {
    std::string error_str = j["error"]["type"].get<std::string>();
    auto ec = magic_enum::enum_cast<ErrorCode>(
        std::string_view{error_str.c_str(), error_str.length()});
    return std::string{
        ErrorCodeToString(ec.value_or(ErrorCode::general_error))};
  } catch (...) {
    return std::nullopt;
  }
}

std::string ResponseParser::GetToolName(const EventMessage& event_message) {
  auto j = json::parse(event_message.data);
  return j["content_block"]["name"].get<std::string>();
}

std::string ResponseParser::GetToolId(const EventMessage& event_message) {
  auto j = json::parse(event_message.data);
  return j["content_block"]["id"].get<std::string>();
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

void ResponseParser::AppendText(
    const std::string& text) {  // Split the message into lines.
  m_content.append(text);
}

std::optional<json> ResponseParser::TryJson(std::string_view text) {
  try {
    auto _json = json::parse(trim(text));
    return _json;
  } catch (...) {
    return std::nullopt;
  }
}
}  // namespace assistant::claude
