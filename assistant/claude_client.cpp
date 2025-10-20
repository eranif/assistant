#include "assistant/claude_client.hpp"

namespace assistant {
ClaudeClient::ClaudeClient(const Endpoint& endpoint)
    : OllamaClient(endpoint),
      m_responseParser(std::make_shared<claude::ResponseParser>()) {}

void ClaudeClient::PullModel(const std::string& name, OnResponseCallback cb) {
  OLOG(LogLevel::kWarning) << "Pull model is supported by Ollama clients only";
  cb("Pull model is not supported by Claude", Reason::kFatalError, false);
}

std::optional<json> ClaudeClient::GetModelInfo(
    [[maybe_unused]] const std::string& model) {
  OLOG(LogLevel::kWarning)
      << "GetModelInfo is supported by Ollama clients only";
  return std::nullopt;
}

std::optional<ModelCapabilities> ClaudeClient::GetModelCapabilities(
    const std::string& model) {
  ModelCapabilities flags{ModelCapabilities::kNone};
  AddFlagSet(flags, ModelCapabilities::kTools);
  AddFlagSet(flags, ModelCapabilities::kCompletion);
  AddFlagSet(flags, ModelCapabilities::kInsert);
  AddFlagSet(flags, ModelCapabilities::kThinking);
  return flags;
}

void ClaudeClient::Chat(std::string msg, OnResponseCallback cb,
                        ChatOptions chat_options) {
  assistant::message json_message{"user", msg};
  CreateAndPushChatRequest(json_message, cb, GetModel(), chat_options);
  ProcessChatRequestQueue();
}

void ClaudeClient::CreateAndPushChatRequest(
    std::optional<assistant::message> msg, OnResponseCallback cb,
    std::string model, ChatOptions chat_options) {
  assistant::options opts;

  assistant::messages history;
  if (IsFlagSet(chat_options, ChatOptions::kNoHistory)) {
    if (msg.has_value()) {
      history = {msg.value()};
    }
  } else {
    AddMessage(msg);
    history = GetMessages();
  }

  // Build the request
  assistant::request req{model, history, nullptr, m_stream, "json", ""};
  req["max_tokens"] = GetMaxTokens();

  if (IsFlagSet(chat_options, ChatOptions::kNoTools)) {
    OLOG(LogLevel::kInfo) << "The 'tools' are disabled for the model: '"
                          << model << "' (per user request).";
  } else if (!m_function_table.IsEmpty()) {
    req["tools"] = m_function_table.ToJSON(EndpointKind::anthropic);
  }

  ChatRequest ctx = {
      .callback_ = cb,
      .request_ = req,
      .model_ = std::move(model),
  };
  m_queue.push_back(std::make_shared<ChatRequest>(ctx));
}

void ClaudeClient::ProcessChatRquest(
    std::shared_ptr<ChatRequest> chat_request) {
  try {
    OLOG(LogLevel::kDebug) << "==> " << std::setw(2) << chat_request->request_;
    m_responseParser->Reset();
    std::string model_name = chat_request->request_["model"].get<std::string>();

    // Prepare chat user data.
    ChatContext user_data{
        .client = this,
        .model = model_name,
        .model_can_think = true,
        .chat_context = chat_request,
    };

    {
      std::scoped_lock lk{m_client_mutex};
      m_client_impl.chat_raw_output(chat_request->request_,
                                    &ClaudeClient::OnRawResponse,
                                    static_cast<void*>(&user_data));
    }

    if (!chat_request->func_calls_.empty()) {
      chat_request->InvokeTools(this);
    }
  } catch (std::exception& e) {
    chat_request->callback_(e.what(), Reason::kFatalError, false);
    Shutdown();
  }
}

void ClaudeClient::ProcessChatRequestQueue() {
  OllamaClient::ProcessChatRequestQueue();
}

bool ClaudeClient::OnRawResponse(const std::string& resp, void* user_data) {
  ChatContext* chat_context = reinterpret_cast<ChatContext*>(user_data);
  ClaudeClient* client = dynamic_cast<ClaudeClient*>(chat_context->client);
  return client->HandleResponse(resp, chat_context);
}

bool ClaudeClient::HandleResponse(const std::string& resp,
                                  ChatContext* chat_context) {
  std::shared_ptr<ChatRequest> req = chat_context->chat_context;
  try {
    std::vector<claude::ParseResult> tokens;
    m_responseParser->Parse(resp, [&tokens](claude::ParseResult token) {
      tokens.push_back(std::move(token));
    });

    OLOG(LogLevel::kTrace) << "Processing: " << tokens.size() << " tokens";
    bool cb_result{true};
    bool is_done{false};
    for (const auto& token : tokens) {
      if (!is_done) {
        is_done = token.IsDone();
      }

      if (token.IsToolCall()) {
        FunctionCall fcall{.name = token.GetToolName(),
                           .args = token.GetToolJson(),
                           .invocation_id = token.GetToolId()};

        // Build the AI request message
        assistant::message tool_invoke_msg("assistant", "");
        json content_arr = json::array();
        json tool_use = json::object();
        tool_use["type"] = "tool_use";
        tool_use["id"] = token.GetToolId();
        tool_use["name"] = token.GetToolName();
        tool_use["input"] = fcall.args;
        content_arr.push_back(tool_use);
        tool_invoke_msg["content"] = content_arr;

        OLOG(LogLevel::kDebug)
            << "Got tool request: " << std::setw(2) << tool_invoke_msg;
        req->func_calls_.push_back({tool_invoke_msg, {std::move(fcall)}});
      } else {
        cb_result = req->callback_(token.content, token.GetReason(),
                                   token.IsThinking());
        chat_context->current_response += token.content;
      }
    }

    if (!cb_result || is_done) {
      // Store the AI response, as a message in our history.
      assistant::message msg{std::string{kAssistantRole},
                             chat_context->current_response};
      if (!cb_result) {
        OLOG(LogLevel::kWarning)
            << "User cancelled response processing (callback returned false).";
      }
      OLOG(LogLevel::kInfo) << "<== " << msg;
      AddMessage(std::move(msg));
      return false;
    }
    return cb_result;
  } catch (const std::exception& e) {
    OLOG(LogLevel::kWarning)
        << "ClaudeClient::HandleResponse: got an exception. " << e.what();
    req->callback_(e.what(), Reason::kFatalError, false);
    m_responseParser->Reset();
    return false;  // close the current session.
  }
}

assistant::message ClaudeClient::FormatToolResponse(
    const FunctionCall& fcall, const FunctionResult& func_result) {
  std::stringstream ss;
  // Add the tool response
  if (func_result.isError) {
    ss << "An error occurred while executing tool: '" << fcall.name
       << "'. Reason: " << func_result.text;
  } else {
    ss << "Tool '" << fcall.name << "' completed successfully. Output:\n"
       << func_result.text;
  }

  assistant::message msg{"user", ""};
  json content_array = json::array();
  json res = json::object();
  res["type"] = "tool_result";
  res["tool_use_id"] = fcall.invocation_id.value_or("");
  res["content"] = ss.str();
  content_array.push_back(res);
  msg["content"] = content_array;

  OLOG(LogLevel::kDebug) << std::setw(2) << msg;
  return msg;
}
}  // namespace assistant
