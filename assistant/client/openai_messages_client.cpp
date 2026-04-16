#include "assistant/client/openai_messages_client.hpp"

#include "assistant/chat_completions_response_parser.hpp"

namespace assistant {

OpenAIMessagesClient::OpenAIMessagesClient(const Endpoint& ep)
    : OllamaClient(ep) {}

std::optional<ModelCapabilities> OpenAIMessagesClient::GetModelCapabilities(
    [[maybe_unused]] const std::string& model) {
  ModelCapabilities flags{ModelCapabilities::kNone};
  AddFlagSet(flags, ModelCapabilities::kTools);
  AddFlagSet(flags, ModelCapabilities::kCompletion);
  AddFlagSet(flags, ModelCapabilities::kInsert);
  AddFlagSet(flags, ModelCapabilities::kThinking);
  return flags;
}

void OpenAIMessagesClient::ProcessChatRequest(
    std::shared_ptr<ChatRequest> chat_request) {
  // /v1/chat/completions uses standard "messages" format
  // No conversion needed - messages stay as "messages"

  // Tools are in standard OpenAI format for /v1/chat/completions
  // They should have the structure: {type: "function", function: {...}}
  if (!m_function_table.IsEmpty() && chat_request->request_.contains("tools")) {
    chat_request->request_["tools"] = m_function_table.ToJSON(
        EndpointKind::openai_messages, GetCachingPolicy());
  }

  // Remove parameters unsupported by /v1/chat/completions
  chat_request->request_.erase("keep_alive");
  chat_request->request_.erase("options");
  chat_request->request_["thinking"] = {{"type", "disabled"}};

  try {
    m_responseParser = std::make_unique<chat_completions::ResponseParser>();
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
                              &OpenAIMessagesClient::OnRawResponse,
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

bool OpenAIMessagesClient::OnRawResponse(const std::string& resp,
                                         void* user_data) {
  ChatContext* chat_context = reinterpret_cast<ChatContext*>(user_data);
  OpenAIMessagesClient* client =
      dynamic_cast<OpenAIMessagesClient*>(chat_context->client);
  return client->HandleResponse(resp, chat_context);
}

bool OpenAIMessagesClient::HandleResponse(const std::string& resp,
                                          ChatContext* chat_context) {
  std::shared_ptr<ChatRequest> req = chat_context->chat_context;
  try {
    std::vector<chat_completions::ParseResult> tokens;
    m_responseParser->Parse(resp,
                            [&tokens](chat_completions::ParseResult token) {
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
        // Process each tool call in the result
        for (const auto& tool_call : token.tool_calls) {
          FunctionCall fcall;
          fcall.name = tool_call.name;
          fcall.invocation_id = tool_call.id;

          // Parse the arguments JSON string to a JSON object
          try {
            fcall.args = json::parse(tool_call.arguments_json);
          } catch (const std::exception& e) {
            OLOG(LogLevel::kWarning)
                << "Failed to parse tool call arguments: " << e.what() << "\n"
                << tool_call;
            continue;
          }

          // Build the AI request message in messages API format
          assistant::message tool_invoke_msg;
          tool_invoke_msg["role"] = "assistant";
          tool_invoke_msg["content"] = nullptr;

          // Tool calls array with single call (will be combined if multiple)
          json tool_calls_json = json::array();
          json tc_json;
          tc_json["id"] = tool_call.id;
          tc_json["type"] = "function";
          tc_json["function"]["name"] = tool_call.name;
          tc_json["function"]["arguments"] = tool_call.arguments_json;
          tool_calls_json.push_back(tc_json);

          tool_invoke_msg["tool_calls"] = tool_calls_json;

          OLOG(LogLevel::kDebug)
              << "Got tool request: " << std::setw(2) << tool_invoke_msg;
          req->func_calls_.push_back({tool_invoke_msg, {std::move(fcall)}});
        }
      } else {
        if (token.HasContent()) {
          cb_result = req->callback_(token.content, token.GetReason(), false);
        }
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
      if (!chat_context->current_response.empty()) {
        AddMessage(std::move(msg));
      }

      if (!cb_result) {
        return false;
      }
      return true;
    }
    return cb_result;
  } catch (const std::exception& e) {
    OLOG(LogLevel::kWarning)
        << "OpenAIMessagesClient::HandleResponse: got an exception. "
        << e.what();
    req->callback_(e.what(), Reason::kFatalError, false);
    m_responseParser = std::make_unique<chat_completions::ResponseParser>();
    return false;  // close the current session.
  }
}

void OpenAIMessagesClient::AddToolsResult(
    std::vector<std::pair<FunctionCall, FunctionResult>> result) {
  if (result.empty()) {
    return;
  }
  OLOG(LogLevel::kDebug) << "Processing " << result.size()
                         << " tool calls responses";
  for (const auto& [fcall, reply] : result) {
    // /v1/chat/completions uses the "tool" role for tool responses
    assistant::message tool_response;
    tool_response["role"] = "tool";
    tool_response["tool_call_id"] = fcall.invocation_id.value_or("");
    tool_response["content"] = reply.text;
    AddMessage(std::move(tool_response));
  }
}

}  // namespace assistant
