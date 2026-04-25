#include "assistant/client/client_base.hpp"

#include "assistant/logger.hpp"
#include "assistant/tool.hpp"

namespace assistant {

bool ClientBase::HandleResponse(const assistant::response& resp,
                                ChatContext& chat_user_data) {
  std::shared_ptr<ChatRequest> req = chat_user_data.chat_context;
  if (m_interrupt.load()) {
    req->callback_("Request cancelled by user", assistant::Reason::kCancelled,
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
      assistant::message msg{std::string{kAssistantRole},
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
        assistant::message msg{std::string{kAssistantRole},
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

bool ClientBase::OnResponse(const assistant::response& resp, void* user_data) {
  ChatContext* cud = reinterpret_cast<ChatContext*>(user_data);
  return cud->client->HandleResponse(resp, *cud);
}

void ClientBase::AddMessage(std::optional<assistant::message> msg) {
  m_history.AddMessage(msg);
}

assistant::messages ClientBase::GetMessages() const {
  assistant::messages msgs;
  // First place the system messages
  m_system_messages.with([&msgs](const assistant::messages& sys_msgs) {
    if (sys_msgs.empty()) {
      return;
    }
    msgs.insert(msgs.end(), sys_msgs.begin(), sys_msgs.end());
  });

  // Following by the user messages
  auto user_messages = m_history.GetMessages();
  msgs.insert(msgs.end(), user_messages.begin(), user_messages.end());
  return msgs;
}

void ClientBase::ApplyConfig(const assistant::Config* conf) {
  if (!conf) {
    return;
  }
  auto endpoint = conf->GetEndpoint();

  if (!endpoint) {
    OLOG(LogLevel::kError) << "No endpoint is found!";
    return;
  }
  SetEndpoint(*endpoint);
  SetTransportType(endpoint->transport_);
  m_function_table.ReloadMCPServers(conf);
  m_server_timeout.set_value(conf->GetServerTimeoutSettings());
  m_keep_alive.set_value(conf->GetKeepAlive());
  m_stream = conf->IsStream();
}

void ClientBase::InvokeTools(std::shared_ptr<ChatRequest> request) {
  if (request->func_calls_.empty()) {
    return;
  }

  std::vector<std::pair<FunctionCall, FunctionResult>> tool_call_results;
  for (auto [msg, calls] : request->func_calls_) {
    if (m_interrupt.load()) {
      return;
    }
    AddMessage(std::move(msg));
    for (auto func_call : calls) {
      if (IsInterrupted()) {
        OLOG(LogLevel::kWarning) << "User interrupted.";
        return;
      }
      std::stringstream ss;
      ss << "Invoking tool: '" << func_call.name << "', args:\n";
      auto args = func_call.args.items();
      for (const auto& [name, value] : args) {
        ss << std::setw(2) << "  " << name << " => " << value << "\n";
      }

      request->callback_(ss.str(), Reason::kLogNotice, false);

      FunctionResult result;

      CanInvokeToolResult can_run_tool{.can_invoke = true};
      auto res = GetFunctionTable().CanRunTool(func_call.name, func_call.args);
      if (res.has_value()) {
        can_run_tool = res.value();
      } else if (m_on_invoke_tool_cb) {
        // No function level human-in-the-loop method was registered,
        // try the global method (client level)
        can_run_tool = m_on_invoke_tool_cb(func_call.name, func_call.args);
      }

      if (!can_run_tool.IsAllowed()) {
        result.isError = true;
        result.text = can_run_tool.reason;
        request->callback_(ss.str(), Reason::kToolDenied, false);

      } else {
        ss = {};
        ss << "Permission to run tool: " << func_call.name << " is granted.";
        request->callback_(ss.str(), Reason::kToolAllowed, false);
        result = GetFunctionTable().Call(func_call);

        ss = {};
        ss << "Tool output: " << result;
        request->callback_(ss.str(), Reason::kLogNotice, false);
      }
      tool_call_results.push_back({func_call, result});
    }
  }

  if (!tool_call_results.empty()) {
    AddToolsResult(std::move(tool_call_results));
  }

  CreateAndPushChatRequest(std::nullopt, request->callback_, request->model_,
                           ChatOptions::kDefault, request->finaliser_);
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

std::optional<TokenUsageStats> ClientBase::GetTokenUsageStats() const {
  auto last_usage = GetLastRequestUsage();
  if (!last_usage) {
    return std::nullopt;
  }

  TokenUsageStats stats;
  stats.context_size = GetContextSize();
  stats.max_tokens = GetMaxTokens();

  if (last_usage.has_value()) {
    stats.input_tokens = last_usage->input_tokens;
    stats.output_tokens = last_usage->output_tokens;
    stats.cache_creation_input_tokens = last_usage->cache_creation_input_tokens;
    stats.cache_read_input_tokens = last_usage->cache_read_input_tokens;
    stats.total_tokens_used = last_usage->input_tokens +
                              last_usage->output_tokens +
                              last_usage->cache_creation_input_tokens +
                              last_usage->cache_read_input_tokens;
  }

  return stats;
}

TokenUsageStats ClientBase::GetAggregatedTokenUsageStats() const {
  TokenUsageStats stats;
  stats.context_size = GetContextSize();
  stats.max_tokens = GetMaxTokens();

  Usage aggregated = GetAggregatedUsage();
  stats.input_tokens = aggregated.input_tokens;
  stats.output_tokens = aggregated.output_tokens;
  stats.cache_creation_input_tokens = aggregated.cache_creation_input_tokens;
  stats.cache_read_input_tokens = aggregated.cache_read_input_tokens;
  stats.total_tokens_used = aggregated.input_tokens + aggregated.output_tokens +
                            aggregated.cache_creation_input_tokens +
                            aggregated.cache_read_input_tokens;

  return stats;
}

bool ClientBase::IsNearContextLimit(double threshold_percentage) const {
  // Use aggregated usage to check overall context consumption
  TokenUsageStats stats = GetAggregatedTokenUsageStats();
  return stats.IsNearContextLimit(threshold_percentage);
}
}  // namespace assistant
