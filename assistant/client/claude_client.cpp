#include "assistant/client/claude_client.hpp"

namespace assistant {

namespace {
// Anthropic beta gate for server-side compaction. When this gate is
// active, the request must include an `anthropic-beta` header carrying
// this token AND a `context_management.edits` entry referencing
// `kCompactionStrategyType`.
//
// Reference:
// https://docs.anthropic.com/en/docs/build-with-claude/compaction
constexpr std::string_view kCompactionBetaToken = "compact-2026-01-12";
constexpr std::string_view kCompactionStrategyType = "compact_20260112";

// Build the `context_management` JSON object for a Claude chat request,
// reflecting the active endpoint's `ServerCompaction` settings. Returns an
// empty json (no field) when compaction is disabled.
json BuildCompactionEdit(const ServerCompaction& sc) {
  json edit = json::object();
  edit["type"] = std::string{kCompactionStrategyType};

  // Anthropic enforces a minimum of 50,000 tokens. We forward whatever the
  // user configured and let the server validate; this avoids surprising
  // them with silent clamping.
  edit["trigger"] = {{"type", "input_tokens"},
                     {"value", sc.trigger_input_tokens}};
  if (sc.pause_after_compaction) {
    edit["pause_after_compaction"] = true;
  }
  if (sc.instructions.has_value() && !sc.instructions->empty()) {
    edit["instructions"] = sc.instructions.value();
  }
  json out = json::object();
  out["edits"] = json::array({std::move(edit)});
  return out;
}

}  // namespace

ClaudeClient::ClaudeClient(const Endpoint& endpoint)
    : OllamaClient(endpoint),
      m_responseParser(std::make_shared<claude::ResponseParser>()) {
  m_multi_tool_reply_as_array = true;
}

std::unordered_map<std::string, std::string> ClaudeClient::GetHttpHeaders()
    const {
  auto headers = m_endpoint.get_value().headers_;
  if (m_endpoint.get_value().server_compaction_.enabled) {
    // anthropic-beta is comma-separated; preserve any value the operator
    // configured by appending rather than overwriting.
    auto it = headers.find("anthropic-beta");
    std::string beta_value{kCompactionBetaToken};
    if (it != headers.end() && !it->second.empty()) {
      // Avoid duplicating the token if already present.
      if (it->second.find(kCompactionBetaToken) == std::string::npos) {
        beta_value = it->second + "," + std::string{kCompactionBetaToken};
      } else {
        beta_value = it->second;
      }
    }
    headers["anthropic-beta"] = std::move(beta_value);
  }
  return headers;
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

void ClaudeClient::CreateAndPushChatRequest(
    std::optional<assistant::message> msg, OnResponseCallback cb,
    std::string model, ChatOptions chat_options,
    std::shared_ptr<ChatRequestFinaliser> finaliser) {
  assistant::options opts;

  assistant::messages history;
  if (IsFlagSet(chat_options, ChatOptions::kNoHistory)) {
    if (msg.has_value()) {
      history = {msg.value()};
    }
  } else {
    AddMessage(msg, MessageType::kNormal);
    history = GetMessages();
  }

  // Build the request
  assistant::request req{assistant::message_type::chat};
  // Toosl goes first, followed by "system" block
  // this is done like this to allow caching on the server side
  // for cost reduction.
  if (IsFlagSet(chat_options, ChatOptions::kNoTools)) {
    OLOG(LogLevel::kInfo) << "The 'tools' are disabled for the model: '"
                          << model << "' (per user request).";
  } else if (!m_function_table.IsEmpty()) {
    req["tools"] =
        m_function_table.ToJSON(EndpointKind::anthropic, GetCachingPolicy());
  }

  // System message: unlike Ollama, Claude accepts a single top level
  // "system" property in the request.
  std::vector<json> system_messages;
  m_system_messages.with(
      [&system_messages, this](const assistant::messages& sys_messages) {
        if (sys_messages.empty()) {
          return;
        }
        for (const auto& msg : sys_messages) {
          if (msg.contains("content") && msg["content"].is_string()) {
            system_messages.push_back(json{
                {"type", "text"}, {"text", msg["content"].get<std::string>()}});
          }
        }

        if (!system_messages.empty() &&
            GetCachingPolicy() == CachePolicy::kStatic) {
          auto& last_message = system_messages.back();
          last_message["cache_control"] = {{"type", "ephemeral"}};
        }
      });

  if (!system_messages.empty()) {
    req["system"] = system_messages;
  }

  if (GetCachingPolicy() == CachePolicy::kAuto) {
    // Enable auto caching. as per the docs:
    // https://platform.claude.com/docs/en/build-with-claude/prompt-caching#automatic-caching
    req["cache_control"] = {{"type", "ephemeral"}};
  }

  // Server-side compaction (beta). When enabled in the active endpoint,
  // attach a `context_management.edits` entry. The `anthropic-beta` header
  // is added by GetHttpHeaders() when the underlying transport is
  // constructed in OllamaClient::CreateClient().
  {
    const auto& sc = m_endpoint.get_value().server_compaction_;
    if (sc.enabled) {
      req["context_management"] = BuildCompactionEdit(sc);
    }
  }

  req["model"] = model;
  req["stream"] = m_stream.load();
  req["messages"] = history.to_json();
  req["max_tokens"] = GetMaxTokens();

  ChatRequest ctx = {
      .callback_ = cb,
      .request_ = req,
      .model_ = std::move(model),
      .finaliser_ = finaliser,
  };
  m_queue.push_back(std::make_shared<ChatRequest>(ctx));
}

void ClaudeClient::ProcessChatRequest(
    std::shared_ptr<ChatRequest> chat_request) {
  try {
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
      InvokeTools(chat_request);
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
      if (token.NeedMoreData()) {
        return true;
      }
      if (!is_done) {
        is_done = token.IsDone();
      }

      if (token.IsCompaction()) {
        // Server-side compaction summary. Surface to the caller as an
        // informational event and stash the summary so we can store it as
        // a structured assistant content block when the turn completes.
        chat_context->compaction_summary = token.content;
        cb_result = req->callback_(
            "History has been updated with server-side history compaction.",
            Reason::kServerCompaction,
            /*thinking=*/false);
        OLOG(LogLevel::kInfo)
            << "Server-side compaction triggered. Summary length: "
            << token.content.size() << " bytes.";
      } else if (token.IsToolCall()) {
        FunctionCall fcall{.name = token.GetToolName(),
                           .args = token.GetToolJson(),
                           .invocation_id = token.GetToolId()};

        // Build the AI request message. If we just observed a compaction
        // block in this turn, prepend it (only on the first tool_use entry)
        // so it persists in history and the API drops earlier messages on
        // future turns.
        assistant::message tool_invoke_msg("assistant", "");
        json content_arr = json::array();
        if (chat_context->compaction_summary.has_value() &&
            req->func_calls_.empty()) {
          json compaction_block = json::object();
          compaction_block["type"] = "compaction";
          compaction_block["content"] =
              chat_context->compaction_summary.value();
          content_arr.push_back(std::move(compaction_block));
          chat_context->compaction_summary.reset();  // consumed
        }
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
        if (usage.has_value()) {
          if (cost.has_value()) {
            double this_requests_cost =
                usage.value().CalculateCost(cost.value());
            SetLastRequestCost(this_requests_cost);
            std::stringstream ss;
            ss << "Total cost: $" << GetTotalCost() << "\n"
               << "Last request cost: $" << GetLastRequestCost();
            ss << " Cached tokens: "
               << GetAggregatedUsage().cache_creation_input_tokens
               << ", Cached tokens read: "
               << GetAggregatedUsage().cache_read_input_tokens << "\n";
            req->callback_(ss.str(), Reason::kRequestCost, false);
          }
          SetLastRequestUsage(usage.value());
        }
        chat_context->current_response += token.content;
      }
    }

    if (!cb_result || is_done) {
      // Store the AI response as a message in our history. If a compaction
      // summary was produced this turn (without a tool call), the assistant
      // message must be a structured array containing the compaction block
      // so subsequent requests can reuse the compacted prefix.
      assistant::message msg;
      msg["role"] = std::string{kAssistantRole};
      if (chat_context->compaction_summary.has_value()) {
        json content_arr = json::array();
        json compaction_block = json::object();
        compaction_block["type"] = "compaction";
        compaction_block["content"] = chat_context->compaction_summary.value();
        content_arr.push_back(std::move(compaction_block));
        if (!chat_context->current_response.empty()) {
          json text_block = json::object();
          text_block["type"] = "text";
          text_block["text"] = chat_context->current_response;
          content_arr.push_back(std::move(text_block));
        }
        msg["content"] = std::move(content_arr);
        chat_context->compaction_summary.reset();
      } else {
        msg["content"] = chat_context->current_response;
      }
      if (!cb_result) {
        OLOG(LogLevel::kWarning)
            << "User cancelled response processing (callback returned false).";
      }
      OLOG(LogLevel::kInfo) << "<== " << msg;
      AddMessage(std::move(msg), MessageType::kNormal);

      if (!cb_result) {
        return false;
      }
      return true;
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

    auto p = BuildToolResponseContent(fcall, reply);
    if (!p.second.empty()) {
      m_pendingMessages.push_back(p.second);
    }

    res["content"] = p.first;
    content_array.push_back(res);
  }
  assistant::message msg;
  msg["role"] = "user";
  msg["content"] = content_array;
  AddMessage(std::move(msg), MessageType::kToolResponse);
}

void ClaudeClient::Compact() {
  m_history.Compact([](assistant::message& msg) {
    if (msg.contains("content") && msg["content"].is_array()) {
      auto& j_array = msg["content"];
      for (auto& element : j_array) {
        if (element.contains("content") && element["content"].is_string()) {
          element["content"] = kTrimMessage;
        }
      }
    }
  });
}

assistant::messages ClaudeClient::GetMessages() const {
  return m_history.GetMessages();
}

}  // namespace assistant
