#include "assistant/ollama_client.hpp"

#include "assistant/assistantlib.hpp"
#include "assistant/config.hpp"
#include "assistant/cpp-mcp/mcp_logger.h"
#include "assistant/helpers.hpp"
#include "assistant/logger.hpp"

namespace assistant {

OllamaClient::OllamaClient(const Endpoint& ep) {
  m_endpoint.with_mut([ep](Endpoint& endpoint) { endpoint = ep; });
  assistant::show_requests(false);
  assistant::show_replies(false);
  assistant::allow_exceptions(true);
  mcp::set_log_level(mcp::log_level::error);
  Startup();
}

std::unique_ptr<ClientImpl> OllamaClient::CreateClient() {
  auto client = std::make_unique<ClientImpl>();
  auto server_timeout_settings = m_server_timeout.get_value();
  client->setConnectTimeout(server_timeout_settings.GetConnectTimeout().first,
                            server_timeout_settings.GetConnectTimeout().second);
  client->setReadTimeout(server_timeout_settings.GetReadTimeout().first,
                         server_timeout_settings.GetReadTimeout().second);
  client->setWriteTimeout(server_timeout_settings.GetWriteTimeout().first,
                          server_timeout_settings.GetWriteTimeout().second);
  client->setEndpointKind(GetEndpointKind());
  client->setServerURL(GetUrl());
  client->verifySSLCertificate(m_endpoint.get_value().verify_server_ssl_);
  auto headers = GetHttpHeaders();
  httplib::Headers h;
  for (const auto& [header_name, header_value] : headers) {
    h.insert({header_name, header_value});
  }
  client->setHttpHeaders(std::move(h));
  return client;
}

OllamaClient::~OllamaClient() { Shutdown(); }

void OllamaClient::Interrupt() {
  ClientBase::Interrupt();
  std::scoped_lock lk{m_client_impl_ptr_mutex};
  if (m_client_impl_ptr == nullptr) {
    return;
  }
  try {
    m_client_impl_ptr->interrupt();
  } catch (std::exception& e) {
    OLOG(LogLevel::kWarning)
        << "an error occurred while interrupting client. " << e.what();
  }
}

void OllamaClient::ApplyConfig(const assistant::Config* conf) {
  ClientBase::ApplyConfig(conf);
}

std::vector<std::string> OllamaClient::List() {
  auto client = CreateClient();
  try {
    return client->list_models();
  } catch (...) {
    return {};
  }
}

json OllamaClient::ListJSON() {
  try {
    auto client = CreateClient();
    return client->list_model_json();
  } catch (...) {
    return {};
  }
}

std::optional<json> OllamaClient::GetModelInfo(const std::string& model) {
  try {
    auto client = CreateClient();
    OLOG(LogLevel::kInfo) << "Fetching info for model: " << model;
    return client->show_model_info(model);
  } catch (std::exception& e) {
    return std::nullopt;
  }
}

std::optional<ModelCapabilities> OllamaClient::GetOllamaModelCapabilities(
    const std::string& model) {
  auto opt = GetModelInfo(model);
  if (!opt.has_value()) {
    return std::nullopt;
  }

  auto& j = opt.value();
  OLOG(LogLevel::kTrace) << "Model info:";
  OLOG(LogLevel::kTrace) << std::setw(2) << j["capabilities"];
  OLOG(LogLevel::kTrace) << std::setw(2) << j["model_info"];

  ModelCapabilities flags{ModelCapabilities::kNone};
  try {
    auto capabilities = j["capabilities"].get<std::vector<std::string>>();
    for (const auto& c : capabilities) {
      if (c == "completion") {
        AddFlagSet(flags, ModelCapabilities::kCompletion);
      } else if (c == "tools") {
        AddFlagSet(flags, ModelCapabilities::kTools);
      } else if (c == "thinking") {
        AddFlagSet(flags, ModelCapabilities::kThinking);
      } else if (c == "insert") {
        AddFlagSet(flags, ModelCapabilities::kInsert);
      } else if (c == "vision") {
        AddFlagSet(flags, ModelCapabilities::kVision);
      } else {
        std::cerr << "unknown capability: " << c << std::endl;
      }
    }
    return flags;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<ModelCapabilities> OllamaClient::GetModelCapabilities(
    const std::string& model) {
  return GetOllamaModelCapabilities(model);
}

void OllamaClient::PullModel(const std::string& name, OnResponseCallback cb) {
  try {
    auto client = CreateClient();
    if (client->getEndpointKind() != EndpointKind::ollama) {
      OLOG(LogLevel::kWarning)
          << "Pull model is supported by Ollama clients only";
      cb("Pull model is supported by Ollama clients only", Reason::kFatalError,
         false);
      return;
    }
    std::stringstream ss;
    ss << "Pulling model: " << name;
    cb(ss.str(), Reason::kLogNotice, false);
    client->pull_model(name, true);
    cb("Model successfully pulled.", Reason::kDone, false);
  } catch (std::exception& e) {
    cb(e.what(), Reason::kFatalError, false);
  }
}

bool OllamaClient::IsRunning() {
  auto client = CreateClient();
  try {
    return client->is_running();
  } catch ([[maybe_unused]] const std::exception& e) {
    return false;
  }
}

void OllamaClient::ProcessChatRquest(
    std::shared_ptr<ChatRequest> chat_request) {
  try {
    OLOG(LogLevel::kDebug) << "==> " << chat_request->request_;

    std::string model_name = chat_request->request_["model"].get<std::string>();

    // Prepare chat user data.
    ChatContext user_data{
        .client = this, .model = model_name, .chat_context = chat_request};
    user_data.model_can_think =
        ModelHasCapability(model_name, ModelCapabilities::kThinking);
    user_data.thinking_start_tag = "<think>";
    user_data.thinking_end_tag = "</think>";

    {
      auto client = CreateClient();
      SetInterruptClientLocker locker{this, client.get()};
      client->chat(chat_request->request_, &OllamaClient::OnResponse,
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

void OllamaClient::Chat(std::string msg, OnResponseCallback cb,
                        ChatOptions chat_options) {
  assistant::message json_message{"user", msg};
  CreateAndPushChatRequest(json_message, cb, GetModel(), chat_options);
  ProcessChatRequestQueue();
}

void OllamaClient::ProcessChatRequestQueue() {
  while (!m_queue.empty()) {
    if (m_interrupt.load()) {
      break;
    }
    ProcessChatRquest(m_queue.pop_front_and_return());
  }
}

void OllamaClient::CreateAndPushChatRequest(
    std::optional<assistant::message> msg, OnResponseCallback cb,
    std::string model, ChatOptions chat_options) {
  assistant::options opts;
  opts["num_ctx"] = GetContextSize();

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
  assistant::request req{model,    history, opts,
                         m_stream, "json",  m_keep_alive.get_value()};
  if (IsFlagSet(chat_options, ChatOptions::kNoTools)) {
    OLOG(LogLevel::kInfo) << "The 'tools' are disabled for the model: '"
                          << model << "' (per user request).";
  } else if (ModelHasCapability(model, ModelCapabilities::kTools) &&
             !m_function_table.IsEmpty()) {
    req["tools"] = m_function_table.ToJSON(EndpointKind::ollama);
  } else {
    OLOG(LogLevel::kWarning)
        << "The selected model: " << model << " does not support 'tools'";
  }

  ChatRequest ctx = {
      .callback_ = cb,
      .request_ = req,
      .model_ = std::move(model),
  };
  m_queue.push_back(std::make_shared<ChatRequest>(ctx));
}

assistant::message OllamaClient::FormatToolResponse(
    const FunctionCall& fcall, const FunctionResult& func_result) {
  std::stringstream ss;
  // Add the tool response
  if (func_result.isError) {
    ss << "An error occurred while executing tool: '" << fcall.name
       << "'. Reason: " << func_result.text;
    OLOG(LogLevel::kWarning) << ss.str();
  } else {
    ss << "Tool '" << fcall.name << "' completed successfully. Output:\n"
       << func_result.text;
    OLOG(LogLevel::kInfo) << ss.str();
  }
  assistant::message msg{"tool", ss.str()};
  return msg;
}
}  // namespace assistant
