#include "assistant/client/openai_client.hpp"

#include "assistant/openai_response_parser.hpp"

namespace assistant {
OpenAIClient::OpenAIClient(const Endpoint& ep) : OllamaClient(ep) {}

std::optional<ModelCapabilities> OpenAIClient::GetModelCapabilities(
    [[maybe_unused]] const std::string& model) {
  ModelCapabilities flags{ModelCapabilities::kNone};
  AddFlagSet(flags, ModelCapabilities::kTools);
  AddFlagSet(flags, ModelCapabilities::kCompletion);
  AddFlagSet(flags, ModelCapabilities::kInsert);
  AddFlagSet(flags, ModelCapabilities::kThinking);
  return flags;
}

void OpenAIClient::ProcessChatRquest(
    std::shared_ptr<ChatRequest> chat_request) {
  // /v1/responses uses "input" instead of "messages"
  if (chat_request->request_.contains("messages")) {
    chat_request->request_["input"] = chat_request->request_["messages"];
    chat_request->request_.erase("messages");
  }
  // Re-serialize tools in /v1/responses format (flat, not nested under
  // "function")
  if (!m_function_table.IsEmpty() && chat_request->request_.contains("tools")) {
    chat_request->request_["tools"] =
        m_function_table.ToJSON(EndpointKind::openai, GetCachingPolicy());
  }
  // Remove parameters unsupported by /v1/responses
  chat_request->request_.erase("keep_alive");
  chat_request->request_.erase("options");
  try {
    OLOG(LogLevel::kDebug) << "==> " << std::setw(2) << chat_request->request_;
    m_responseParser = std::make_unique<OpenAIResponseParser>();
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
                              &OpenAIClient::OnRawResponse,
                              static_cast<void*>(&user_data));
    }

    if (!chat_request->func_calls_.empty()) {
      chat_request->InvokeTools(this, chat_request->finaliser_);
    }
  } catch (std::exception& e) {
    chat_request->callback_(e.what(), Reason::kFatalError, false);
    Shutdown();
  }
}

bool OpenAIClient::OnRawResponse(const std::string& resp, void* user_data) {
  ChatContext* chat_context = reinterpret_cast<ChatContext*>(user_data);
  OpenAIClient* client = dynamic_cast<OpenAIClient*>(chat_context->client);
  return client->HandleResponse(resp, chat_context);
}

bool OpenAIClient::HandleResponse(const std::string& resp,
                                  ChatContext* chat_context) {
  std::shared_ptr<ChatRequest> req = chat_context->chat_context;
  try {
    std::vector<OpenAIResponseParser::ParseResult> tokens;
    m_responseParser->Parse(resp,
                            [&tokens](OpenAIResponseParser::ParseResult token) {
                              tokens.push_back(std::move(token));
                            });

    bool cb_result{true};
    bool is_done{false};
    for (const auto& token : tokens) {
      if (token.NeedMoreData()) {
        return true;
      }
      if (!is_done) {
        is_done = token.IsDone();
      }

      if (token.IsToolCall()) {
        FunctionCall fcall{.name = token.GetToolName(),
                           .args = token.GetToolJson(),
                           .invocation_id = token.GeToolCallId()};

        // Build the AI request message
        assistant::message tool_invoke_msg;
        tool_invoke_msg["type"] = "function_call";
        tool_invoke_msg["call_id"] = token.GeToolCallId();
        tool_invoke_msg["name"] = token.GetToolName();
        tool_invoke_msg["arguments"] = fcall.args.dump();

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
          ss << " Cached tokens: "
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

      if (!cb_result) {
        return false;
      }
      return true;
    }
    return cb_result;
  } catch (const std::exception& e) {
    OLOG(LogLevel::kWarning)
        << "ClaudeClient::HandleResponse: got an exception. " << e.what();
    req->callback_(e.what(), Reason::kFatalError, false);
    m_responseParser = std::make_unique<OpenAIResponseParser>();
    return false;  // close the current session.
  }
}

void OpenAIClient::AddToolsResult(
    std::vector<std::pair<FunctionCall, FunctionResult>> result) {
  if (result.empty()) {
    return;
  }
  OLOG(LogLevel::kDebug) << "Processing " << result.size()
                         << " tool calls responses";
  for (const auto& [fcall, reply] : result) {
    assistant::message tool_response;
    tool_response["type"] = "function_call_output";
    tool_response["call_id"] = fcall.invocation_id.value_or("");
    tool_response["output"] = reply.text;
    AddMessage(std::move(tool_response));
  }
}

}  // namespace assistant
