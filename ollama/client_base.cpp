#include "ollama/client_base.hpp"

#include "ollama/logger.hpp"
#include "ollama/tool.hpp"

namespace ollama {

void ClientBase::ProcessQueue() {
  while (!m_queue.empty()) {
    if (m_interrupt) {
      SoftReset();
      break;
    }
    ProcessContext(m_queue.pop_front_and_return());
  }
}

bool ClientBase::HandleResponse(const ollama::response& resp,
                                ChatUserData& chat_user_data) {
  std::shared_ptr<ChatContext> req = chat_user_data.chat_context;
  if (m_interrupt) {
    m_interrupt = false;
    req->callback_("Request cancelled by user", ollama::Reason::kCancelled,
                   false);
    SoftReset();
    return false;
  }
  auto calls = ResponseParser::GetTools(resp);
  bool is_done = ResponseParser::IsDone(resp);
  if (calls.has_value() && !calls.value().empty()) {
    // Add the AI response
    auto ai_message_opt = ResponseParser::GetResponseMessage(resp);
    if (ai_message_opt.has_value()) {
      auto func_calls = calls.value();
      req->func_calls_.push_back(
          {ai_message_opt.value(), std::move(calls.value())});
    }
  } else {
    auto content = ResponseParser::GetContent(resp);
    auto reason = (is_done && req->func_calls_.empty())
                      ? Reason::kDone
                      : Reason::kPartialResult;

    bool token_is_part_of_thinking_process = chat_user_data.thinking;
    if (chat_user_data.model_can_think && content.has_value()) {
      if (chat_user_data.thinking &&
          content.value() == chat_user_data.thinking_end_tag) {
        // we change the inner state to "not thinking"
        // but we want this token to be reported as "thinking" to the caller.
        chat_user_data.thinking = false;
        token_is_part_of_thinking_process = true;
      } else if (!chat_user_data.thinking &&
                 content.value() == chat_user_data.thinking_start_tag) {
        // Thinking process started.
        chat_user_data.thinking = true;
        token_is_part_of_thinking_process = true;
      }
    }

    if (content.has_value()) {
      req->callback_(content.value(), reason,
                     token_is_part_of_thinking_process);
    } else if (is_done) {
      req->callback_({}, reason, token_is_part_of_thinking_process);
    }

    if (content.has_value()) {
      m_current_response += content.value();
    }

    switch (reason) {
      case Reason::kDone:
      case Reason::kFatalError: {
        // Store the AI response, as a message in our history.
        ollama::message msg{std::string{kAssistantRole}, m_current_response};
        OLOG(LogLevel::kDebug) << "<== " << msg;
        AddMessage(std::move(msg));
        m_current_response.clear();
      } break;
      default:
        break;
    }
  }
  return !is_done;
}

bool ClientBase::OnResponse(const ollama::response& resp, void* user_data) {
  ChatUserData* cud = reinterpret_cast<ChatUserData*>(user_data);
  return cud->client->HandleResponse(resp, *cud);
}

void ClientBase::ProcessContext(std::shared_ptr<ChatContext> context) {
  try {
    OLOG(LogLevel::kDebug) << "==> " << context->request_;

    std::string model_name = context->request_["model"].get<std::string>();

    // Prepare chat user data.
    ChatUserData user_data{
        .client = this, .model = model_name, .chat_context = context};
    user_data.model_can_think =
        ModelHasCapability(model_name, ModelCapabilities::kThinking);
    if (user_data.model_can_think) {
      auto iter = m_model_options.find(model_name);
      if (iter != m_model_options.end()) {
        const auto& mo = iter->second;
        user_data.thinking_start_tag = mo.think_start_tag;
        user_data.thinking_end_tag = mo.think_end_tag;
      }
    }

    ChatImpl(context->request_, &ClientBase::OnResponse,
             static_cast<void*>(&user_data));

    if (!context->func_calls_.empty()) {
      context->InvokeTools(this);
    }
  } catch (std::exception& e) {
    context->callback_(e.what(), Reason::kFatalError, false);
    SoftReset();
  }
}

void ClientBase::CreateAndPushContext(std::optional<ollama::message> msg,
                                      OnResponseCallback cb, std::string model,
                                      ChatOptions chat_options) {
  ollama::options opts;

  std::optional<bool> think, hidethinking;
  auto where = m_model_options.find(model);
  if (where == m_model_options.end()) {
    // Can't fail. We always include the "default"
    where = m_model_options.find("default");
  }

  ModelOptions model_options;
  if (where == m_model_options.end()) {
    OLOG(LogLevel::kWarning) << "Missing 'default' model setup in "
                                "configuration file. Creating and using one.";
    model_options = Config::CreaetDefaultModelOptions();
    m_model_options.insert({model, model_options});
  } else {
    model_options = where->second;
  }
  for (const auto& [name, value] : model_options.options.items()) {
    opts[name] = value;
  }
  think = model_options.think;
  hidethinking = model_options.hidethinking;

  AddMessage(msg);
  auto history = GetMessages();

  // Build the request
  ollama::request req{model, history, opts, m_stream, "json", m_keep_alive};
  if (think.has_value()) {
    req["think"] = think.value();
  }

  if (hidethinking.has_value()) {
    req["hidethinking"] = hidethinking.value();
  }

  if (IsFlagSet(chat_options, ChatOptions::kNoTools)) {
    OLOG(LogLevel::kInfo) << "The 'tools' are disabled for the model: '"
                          << model << "' (per user request).";
  } else if (ModelHasCapability(model, ModelCapabilities::kTooling)) {
    req["tools"] = m_function_table.ToJSON();
  } else {
    OLOG(LogLevel::kWarning)
        << "The selected model: " << model << " does not support 'tools'";
  }

  ChatContext ctx = {
      .callback_ = cb,
      .request_ = req,
      .model_ = std::move(model),
  };
  m_queue.push_back(std::make_shared<ChatContext>(ctx));
}

void ClientBase::Chat(std::string msg, OnResponseCallback cb, std::string model,
                      ChatOptions chat_options) {
  ollama::message ollama_message{"user", msg};
  CreateAndPushContext(ollama_message, cb, model, chat_options);
  ProcessQueue();
}

void ClientBase::AddMessage(std::optional<ollama::message> msg) {
  if (!msg.has_value()) {
    return;
  }

  m_messages.push_back(std::move(*msg));
  // truncate the history to fit the window size
  if (m_messages.size() >= m_windows_size) {
    m_messages.erase(m_messages.begin());
  }
}

ollama::messages ClientBase::GetMessages() const {
  ollama::messages msgs;
  msgs.reserve(m_system_messages.size() + m_messages.size());

  if (!m_system_messages.empty()) {
    msgs.insert(msgs.end(), m_system_messages.begin(), m_system_messages.end());
  }
  if (!m_messages.empty()) {
    msgs.insert(msgs.end(), m_messages.begin(), m_messages.end());
  }
  return msgs;
}

void ClientBase::AddSystemMessage(const std::string& msg) {
  m_system_messages.push_back(ollama::message{"system", msg});
}

void ClientBase::ClearSystemMessages() { m_system_messages.clear(); }

void ClientBase::ApplyConfig(const ollama::Config* conf) {
  if (!conf) {
    return;
  }
  auto endpoint = conf->GetEndpoint();
  if (!endpoint) {
    OLOG(LogLevel::kError) << "No endpoint is found!";
    return;
  }

  m_url = endpoint->GetUrl();
  m_windows_size = conf->GetHistorySize();
  m_function_table.ReloadMCPServers(conf);
  m_model_options = conf->GetModelOptionsMap();
  m_default_model_options = Config::CreaetDefaultModelOptions();
  m_http_headers = endpoint->GetHeaders();
  auto iter = conf->GetModelOptionsMap().find("default");
  if (iter != conf->GetModelOptionsMap().end()) {
    m_default_model_options = iter->second;
  }
  m_keep_alive = conf->GetKeepAlive();
  m_stream = conf->IsStream();
  SetLogLevel(conf->GetLogLevel());
}

void ChatContext::InvokeTools(ClientBase* client) {
  if (func_calls_.empty()) {
    return;
  }

  for (auto [msg, calls] : func_calls_) {
    if (client->m_interrupt) {
      return;
    }
    client->AddMessage(std::move(msg));
    for (auto func_call : calls) {
      if (client->m_interrupt) {
        OLOG(LogLevel::kWarning) << "User interrupted.";
        return;
      }
      std::stringstream ss;
      ss << "Invoking tool: '" << func_call.name << "', args:\n";
      auto args = func_call.args.items();
      for (const auto& [name, value] : args) {
        ss << std::setw(2) << "  " << name << " => " << value << "\n";
      }

      callback_(ss.str(), Reason::kLogNotice, false);
      auto result = client->m_function_table.Call(func_call);
      ss = {};
      ss << "Tool output: " << result;
      callback_(ss.str(), Reason::kLogNotice, false);

      ss = {};
      // Add the tool response
      if (result.isError) {
        ss << "An error occurred while executing tool: '" << func_call.name
           << "'. Reason: " << result.text;
        OLOG(LogLevel::kWarning) << ss.str();
      } else {
        ss << "Tool '" << func_call.name
           << "' completed successfully. Output:\n"
           << result.text;
        OLOG(LogLevel::kInfo) << ss.str();
      }
      ollama::message msg{"tool", ss.str()};
      client->AddMessage(std::move(msg));
    }
  }
  client->CreateAndPushContext(std::nullopt, callback_, model_,
                               ChatOptions::kDefault);
}

bool ClientBase::ModelHasCapability(const std::string& model_name,
                                    ModelCapabilities c) {
  if (m_model_capabilities.count(model_name) == 0) {
    // Load the model capabilities
    auto capabilities = GetModelCapabilities(model_name);
    if (capabilities.has_value()) {
      m_model_capabilities.insert({model_name, capabilities.value()});
    } else {
      m_model_capabilities.insert({model_name, ModelCapabilities::kNone});
    }
  }

  auto flags = m_model_capabilities.find(model_name)->second;
  return IsFlagSet(flags, c);
}
}  // namespace ollama
