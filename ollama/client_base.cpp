#include "ollama/client_base.hpp"

#include "ollama/logger.hpp"
#include "ollama/tool.hpp"

namespace ollama {

void ClientBase::ProcessQueue() {
  while (!m_queue.empty()) {
    if (m_interrupt.load()) {
      break;
    }
    ProcessContext(m_queue.pop_front_and_return());
  }
}

bool ClientBase::HandleResponse(const ollama::response& resp,
                                ChatContext& chat_user_data) {
  std::shared_ptr<ChatRequest> req = chat_user_data.chat_context;
  if (m_interrupt.load()) {
    req->callback_("Request cancelled by user", ollama::Reason::kCancelled,
                   false);
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

    bool cb_result{true};
    if (content.has_value()) {
      cb_result = req->callback_(content.value(), reason,
                                 token_is_part_of_thinking_process);
    } else if (is_done) {
      cb_result = req->callback_({}, reason, token_is_part_of_thinking_process);
    }

    if (content.has_value()) {
      chat_user_data.current_response += content.value();
    }

    if (cb_result == false) {
      // Store the AI response, as a message in our history.
      ollama::message msg{std::string{kAssistantRole},
                          chat_user_data.current_response};
      OLOG(LogLevel::kWarning)
          << "User cancelled response processing (callback returned false)."
          << msg;
      OLOG(LogLevel::kInfo) << "<== " << msg;
      AddMessage(std::move(msg));
      return false;
    }

    switch (reason) {
      case Reason::kDone:
      case Reason::kFatalError: {
        // Store the AI response, as a message in our history.
        ollama::message msg{std::string{kAssistantRole},
                            chat_user_data.current_response};
        OLOG(LogLevel::kDebug) << "<== " << msg;
        AddMessage(std::move(msg));
      } break;
      default:
        break;
    }
  }
  return !is_done;
}

bool ClientBase::OnResponse(const ollama::response& resp, void* user_data) {
  ChatContext* cud = reinterpret_cast<ChatContext*>(user_data);
  return cud->client->HandleResponse(resp, *cud);
}

void ClientBase::ProcessContext(std::shared_ptr<ChatRequest> chat_request) {
  try {
    OLOG(LogLevel::kDebug) << "==> " << chat_request->request_;

    std::string model_name = chat_request->request_["model"].get<std::string>();

    // Prepare chat user data.
    ChatContext user_data{
        .client = this, .model = model_name, .chat_context = chat_request};
    user_data.model_can_think =
        ModelHasCapability(model_name, ModelCapabilities::kThinking);
    if (user_data.model_can_think) {
      m_model_options.with(
          [&user_data,
           &model_name](const std::unordered_map<std::string, ModelOptions>&
                            model_options) {
            auto iter = model_options.find(model_name);
            if (iter == model_options.end()) {
              return;
            }
            const auto& mo = iter->second;
            user_data.thinking_start_tag = mo.think_start_tag;
            user_data.thinking_end_tag = mo.think_end_tag;
          });
    }

    ChatImpl(chat_request->request_, &ClientBase::OnResponse,
             static_cast<void*>(&user_data));

    if (!chat_request->func_calls_.empty()) {
      chat_request->InvokeTools(this);
    }
  } catch (std::exception& e) {
    chat_request->callback_(e.what(), Reason::kFatalError, false);
    Shutdown();
  }
}

void ClientBase::CreateAndPushContext(std::optional<ollama::message> msg,
                                      OnResponseCallback cb, std::string model,
                                      ChatOptions chat_options) {
  ollama::options opts;

  std::optional<bool> think, hidethinking;
  ModelOptions model_options;
  m_model_options.with([&model_options, &model](const auto& m) {
    auto where = m.find(model);
    if (where == m.end()) {
      // Can't fail. We always include the "default"
      OLOG(LogLevel::kWarning) << "Missing 'default' model setup in "
                                  "configuration file. Creating and using one.";
      model_options = m.find("default")->second;
    } else {
      model_options = where->second;
    }
  });

  for (const auto& [name, value] : model_options.options.items()) {
    opts[name] = value;
  }
  think = model_options.think;
  hidethinking = model_options.hidethinking;

  ollama::messages history;
  if (IsFlagSet(chat_options, ChatOptions::kNoHistory)) {
    if (msg.has_value()) {
      history = {msg.value()};
    }
  } else {
    AddMessage(msg);
    history = GetMessages();
  }

  // Build the request
  ollama::request req{model,    history, opts,
                      m_stream, "json",  m_keep_alive.get_value()};
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

  ChatRequest ctx = {
      .callback_ = cb,
      .request_ = req,
      .model_ = std::move(model),
  };
  m_queue.push_back(std::make_shared<ChatRequest>(ctx));
}

void ClientBase::Chat(std::string msg, OnResponseCallback cb, std::string model,
                      ChatOptions chat_options) {
  ollama::message ollama_message{"user", msg};
  CreateAndPushContext(ollama_message, cb, model, chat_options);
  ProcessQueue();
}

void ClientBase::AddMessage(std::optional<ollama::message> msg) {
  m_messages.with_mut([msg = std::move(msg), this](ollama::messages& msgs) {
    if (!msg.has_value()) {
      return;
    }
    msgs.push_back(std::move(*msg));
    // truncate the history to fit the window size
    if (msgs.size() >= m_windows_size) {
      msgs.erase(msgs.begin());
    }
  });
}

ollama::messages ClientBase::GetMessages() const {
  ollama::messages msgs;
  m_system_messages.with([&msgs](const ollama::messages& sysmsgs) {
    msgs.insert(msgs.end(), sysmsgs.begin(), sysmsgs.end());
  });
  m_system_messages.with([&msgs](const ollama::messages& m) {
    if (m.empty()) {
      return;
    }
    msgs.insert(msgs.end(), m.begin(), m.end());
  });

  m_messages.with([&msgs](const ollama::messages& m) {
    if (m.empty()) {
      return;
    }
    msgs.insert(msgs.end(), m.begin(), m.end());
  });
  return msgs;
}

void ClientBase::ApplyConfig(const ollama::Config* conf) {
  if (!conf) {
    return;
  }
  auto endpoint = conf->GetEndpoint();
  if (!endpoint) {
    OLOG(LogLevel::kError) << "No endpoint is found!";
    return;
  }

  m_url.set_value(endpoint->GetUrl());
  m_windows_size.store(conf->GetHistorySize());
  m_function_table.ReloadMCPServers(conf);
  m_model_options.set_value(conf->GetModelOptionsMap());
  m_default_model_options.set_value(Config::CreaetDefaultModelOptions());
  m_http_headers.set_value(endpoint->GetHeaders());
  m_server_timeout.set_value(conf->GetServerTimeoutSettings());
  auto iter = conf->GetModelOptionsMap().find("default");
  if (iter != conf->GetModelOptionsMap().end()) {
    m_default_model_options.set_value(iter->second);
  }
  m_keep_alive.set_value(conf->GetKeepAlive());
  m_stream = conf->IsStream();
  SetLogLevel(conf->GetLogLevel());
}

void ChatRequest::InvokeTools(ClientBase* client) {
  if (func_calls_.empty()) {
    return;
  }

  for (auto [msg, calls] : func_calls_) {
    if (client->m_interrupt.load()) {
      return;
    }
    client->AddMessage(std::move(msg));
    for (auto func_call : calls) {
      if (client->IsInterrupted()) {
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
      auto result = client->GetFunctionTable().Call(func_call);
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
  bool found{false};
  m_model_capabilities.with_mut([&found, &model_name, c, this](auto& map) {
    if (map.count(model_name) == 0) {
      // Load the model capabilities
      auto capabilities = GetModelCapabilities(model_name);
      if (capabilities.has_value()) {
        map.insert({model_name, capabilities.value()});
      } else {
        map.insert({model_name, ModelCapabilities::kNone});
      }
    }
    auto flags = map.find(model_name)->second;
    found = IsFlagSet(flags, c);
  });
  return found;
}
}  // namespace ollama
