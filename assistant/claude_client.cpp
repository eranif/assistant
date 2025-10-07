#include "assistant/claude_client.hpp"

namespace assistant {
ClaudeClient::ClaudeClient(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)
    : OllamaClient(url, headers) {}

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
  return flags;
}

void ClaudeClient::Chat(std::string msg, OnResponseCallback cb,
                        std::string model, ChatOptions chat_options) {
  assistant::message json_message{"user", msg};
  CreateAndPushChatRequest(json_message, cb, model, chat_options);
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
  } else {
    req["tools"] = m_function_table.ToJSON(EndpointKind::claude);
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
    OLOG(LogLevel::kDebug) << "==> " << chat_request->request_;

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
  std::cout << resp;
  std::cout.flush();
  return true;
}

}  // namespace assistant
