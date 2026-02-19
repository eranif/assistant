#include "assistant/claude_client.hpp"

namespace assistant {
ClaudeClient::ClaudeClient(const Endpoint& endpoint)
    : OllamaClient(endpoint),
      m_responseParser(std::make_shared<claude::ResponseParser>()) {
  m_multi_tool_reply_as_array = true;
}

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

  // System message: unlike Ollama, Claude accepts a single top level
  // "system" property in the request.
  std::vector<json> system_messages;
  m_system_messages.with(
      [&system_messages](const assistant::messages& sys_messages) {
        if (sys_messages.empty()) {
          return;
        }
        for (const auto& msg : sys_messages) {
          if (msg.contains("content") && msg["content"].is_string()) {
            system_messages.push_back(json{
                {"type", "text"}, {"text", msg["content"].get<std::string>()}});
          }
        }

        if (!system_messages.empty()) {
          auto& last_msg = system_messages.back();
          last_msg["cache_control"] = json{{"type", "ephemeral"}};
        }
      });

  if (!system_messages.empty()) {
    req["system"] = system_messages;
  }

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
      auto client = CreateClient();
      SetInterruptClientLocker locker{this, client.get()};
      client->chat_raw_output(chat_request->request_,
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
        auto usage = token.GetUsage();
        auto cost = GetPricing();
        if (usage.has_value() && cost.has_value()) {
          double this_requests_cost = usage.value().CalculateCost(cost.value());
          SetLastRequestCost(this_requests_cost);
          std::stringstream ss;
          ss << "Total cost: $" << GetTotalCost() << "\n"
             << "Last request cost: $" << GetLastRequestCost();
          ss << "Cached tokens: "
             << GetAggregatedUsage().cache_creation_input_tokens
             << ", Cached tokens read: "
             << GetAggregatedUsage().cache_read_input_tokens << "\n";
          req->callback_(ss.str(), Reason::kRequestCost, false);
          SetLastRequestUsage(usage.value());
        }
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
    OLOG(LogLevel::kWarning)
        << claude::ResponseParser::GetErrorMessage(resp).value_or("");
    req->callback_(e.what(), Reason::kFatalError, false);
    m_responseParser->Reset();
    return false;  // close the current session.
  }
}

void ClaudeClient::AddToolsResult(
    std::vector<std::pair<FunctionCall, FunctionResult>> result) {
  if (result.empty()) {
    return;
  }

  OLOG(LogLevel::kDebug) << "Processing " << result.size()
                         << " tool calls responses";
  json content_array = json::array();
  for (const auto& [fcall, reply] : result) {
    json res = json::object();
    res["type"] = "tool_result";
    res["tool_use_id"] = fcall.invocation_id.value_or("");
    res["content"] = reply.text;
    content_array.push_back(res);
  }
  assistant::message msg{"user", ""};
  msg["content"] = content_array;
  AddMessage(std::move(msg));
}

assistant::messages ClaudeClient::GetMessages() const {
  assistant::messages msgs;
  // Following by the user messages
  m_messages.with([&msgs](const assistant::messages& m) {
    if (m.empty()) {
      return;
    }
    msgs.insert(msgs.end(), m.begin(), m.end());
  });
  return msgs;
}

}  // namespace assistant
