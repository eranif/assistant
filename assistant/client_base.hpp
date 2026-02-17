#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "assistant/attributes.hpp"
#include "assistant/config.hpp"

namespace assistant {

constexpr std::string_view kAssistantRole = "assistant";

enum class Reason {
  /// The current reason completed successfully.
  kDone,
  /// More data to come.
  kPartialResult,
  /// A non recoverable error.
  kFatalError,
  /// Log messages - NOTICE
  kLogNotice,
  /// Log messages - DEBUG
  kLogDebug,
  /// Request cancelled by the user.
  kCancelled,
  /// Cost
  kRequestCost,
};

enum class ModelCapabilities {
  kNone = (0),
  kThinking = (1 << 0),
  kTools = (1 << 1),
  kCompletion = (1 << 2),
  kInsert = (1 << 3),
  kVision = (1 << 4),
};

/// Options passed to the "Chat()" API call.
enum class ChatOptions {
  /// Default. All is enabled.
  kDefault = (0),
  /// Do not pass the tools to the current chat request.
  kNoTools = (1 << 0),
  /// Do not pass the chat history to the current chat request.
  kNoHistory = (1 << 1),
};

using OnResponseCallback = std::function<bool(
    const std::string& text, Reason call_reason, bool thinking)>;

/// Called when a tool is about to be invoked.
using OnToolInvokeCallback = std::function<bool(const std::string& tool_name)>;

/// Tokens pricing
struct Pricing {
  double input_tokens{0.0};                 // $ per 1 token
  double cache_creation_input_tokens{0.0};  // $ per 1 token
  double cache_read_input_tokens{0.0};      // $ per 1 token
  double output_tokens{0.0};                // $ per 1 token
};

// Initialized map with per-token prices (USD)
const inline std::unordered_map<std::string, Pricing> CLAUDE_PRICING = {
    {"claude-opus-4-20250514", {0.000015, 0.00001875, 0.0000015, 0.000075}},
    {"claude-opus-4", {0.000015, 0.00001875, 0.0000015, 0.000075}},
    {"claude-sonnet-4", {0.000003, 0.00000375, 0.0000003, 0.000015}},
    {"claude-opus-4-5-20251101", {0.000005, 0.00000625, 0.0000005, 0.000025}},
    {"claude-opus-4-5", {0.000005, 0.00000625, 0.0000005, 0.000025}},
    {"claude-sonnet-4-5-20250929", {0.000003, 0.00000375, 0.0000003, 0.000015}},
    {"claude-sonnet-4-5", {0.000003, 0.00000375, 0.0000003, 0.000015}},
    {"claude-haiku-4-5-20251001", {0.000001, 0.00000125, 0.0000001, 0.000005}},
    {"claude-haiku-4-5", {0.000001, 0.00000125, 0.0000001, 0.000005}},
    {"claude-opus-4-6", {0.000005, 0.00000625, 0.0000005, 0.000025}}};

inline std::optional<Pricing> FindPricing(const std::string& model_name) {
  auto iter = CLAUDE_PRICING.find(model_name);
  if (iter == CLAUDE_PRICING.end()) {
    return std::nullopt;
  }
  return iter->second;
}

class ClientBase;
struct ChatRequest {
  OnResponseCallback callback_;
  assistant::request request_;
  std::string model_;
  /// If a tool(s) invocation is required, it will be placed here. Once we
  /// invoke the tool and push the tool response + the request to the history
  /// and remove it from here.
  std::vector<
      std::pair<std::optional<assistant::message>, std::vector<FunctionCall>>>
      func_calls_;
  void InvokeTools(ClientBase* client);
};

/// We pass this struct to provide context in the callback.
struct ChatContext {
  ClientBase* client{nullptr};
  std::string model;
  bool thinking{false};
  bool model_can_think{false};
  std::shared_ptr<ChatRequest> chat_context{nullptr};
  std::string thinking_start_tag{"<think>"};
  std::string thinking_end_tag{"</think>"};
  std::string current_response;
};

struct ChatRequestQueue {
 public:
  ChatRequestQueue() = default;
  ~ChatRequestQueue() = default;

  inline std::shared_ptr<ChatRequest> pop_front_and_return() {
    std::scoped_lock lk{m_mutex};
    if (m_vec.empty()) {
      return nullptr;
    }
    auto fr = m_vec.front();
    m_vec.erase(m_vec.begin());
    return fr;
  }

  inline bool empty() const { return size() == 0; }
  inline void push_back(std::shared_ptr<ChatRequest> c) {
    std::scoped_lock lk{m_mutex};
    m_vec.push_back(c);
  }

  inline void clear() {
    std::scoped_lock lk{m_mutex};
    m_vec.clear();
  }

  inline size_t size() const {
    std::scoped_lock lk{m_mutex};
    return m_vec.size();
  }

 private:
  mutable std::mutex m_mutex;
  std::vector<std::shared_ptr<ChatRequest>> m_vec GUARDED_BY(m_mutex);
};

struct Message {
  std::string role;
  std::string text;

  assistant::message as_message() const {
    return assistant::message{role, text};
  }

  static std::optional<Message> from_message(const assistant::message& j) {
    try {
      Message m;
      m.text = j["content"].get<std::string>();
      m.role = j["role"].get<std::string>();
      return m;
    } catch (...) {
      return std::nullopt;
    }
  }
};

class ClientBase {
 public:
  ClientBase() = default;
  virtual ~ClientBase() { Shutdown(); }

  ///===---------------------------
  /// Client API - START
  ///===---------------------------
  /// Start a chat. Some options of the chat can be controlled via the
  /// ChatOptions flags. For example, user may disable "tools" even though the
  /// model support them.
  virtual void Chat(std::string msg, OnResponseCallback cb,
                    ChatOptions chat_options) = 0;

  /// Return true if the server is running.
  virtual bool IsRunning() = 0;

  /// Return list of models available.
  virtual std::vector<std::string> List() = 0;

  /// Return list of models available using JSON format.
  virtual json ListJSON() = 0;

  /// Pull model from Ollama registry
  virtual void PullModel(const std::string& name, OnResponseCallback cb) = 0;

  virtual std::optional<json> GetModelInfo(const std::string& model) = 0;
  /// Return a bitwise operator model capabilities.
  virtual std::optional<ModelCapabilities> GetModelCapabilities(
      const std::string& model) = 0;

  virtual void CreateAndPushChatRequest(std::optional<assistant::message> msg,
                                        OnResponseCallback cb,
                                        std::string model,
                                        ChatOptions chat_options) = 0;

  /// Format client specific call response message.
  virtual assistant::message FormatToolResponse(
      const FunctionCall& fcall, const FunctionResult& func_result) = 0;

  ///===---------------------------
  /// Client API - END
  ///===---------------------------

  void SetTookInvokeCallback(OnToolInvokeCallback cb) {
    m_on_invoke_tool_cb = std::move(cb);
  }

  virtual void ApplyConfig(const assistant::Config* conf);
  virtual void Startup() { m_interrupt.store(false); }
  virtual void Shutdown() {
    Interrupt();
    ClearMessageQueue();
    ClearSystemMessages();
    ClearHistoryMessages();
    ClearFunctionTable();
  }

  virtual void Interrupt() { m_interrupt.store(true); }
  inline bool IsInterrupted() const { return m_interrupt.load(); }

  /// Set the number of messages to keep when chatting with the model. The
  /// implementation uses a FIFO.
  inline void SetHistorySize(size_t count) { m_windows_size.store(count); }
  inline size_t GetHistorySize() const { return m_windows_size.load(); }

  const FunctionTable& GetFunctionTable() const { return m_function_table; }
  FunctionTable& GetFunctionTable() { return m_function_table; }

  void ClearFunctionTable() { m_function_table.Clear(); }
  void ClearMessageQueue() { m_queue.clear(); }

  /// Add system message to the prompt. System messages are always sent as part
  /// of the prompt
  void AddSystemMessage(const std::string& msg) {
    m_system_messages.with_mut([&msg](assistant::messages& msgs) {
      msgs.push_back(assistant::message{"system", msg});
    });
  }

  /// Clear all system messages.
  void ClearSystemMessages() {
    m_system_messages.with_mut([](assistant::messages& msgs) { msgs.clear(); });
  }

  /// Clear all history messages.
  void ClearHistoryMessages() {
    m_messages.with_mut([](assistant::messages& msgs) { msgs.clear(); });
  }

  /// Return the history messages.
  std::vector<Message> GetHistory() const {
    std::vector<Message> history;
    m_messages.with([&history](const assistant::messages& m) {
      for (const auto& msg : m) {
        auto message = Message::from_message(msg);
        if (message.has_value()) {
          history.push_back(message.value());
        }
      }
    });
    return history;
  }

  /// Replace the history.
  void SetHistory(const std::vector<Message>& history) {
    m_messages.with_mut([&history](assistant::messages& m) {
      m.clear();
      m.reserve(history.size());
      for (const auto& msg : history) {
        m.push_back(msg.as_message());
      }
    });
  }

  inline std::string GetUrl() const { return m_endpoint.get_value().url_; }
  inline std::unordered_map<std::string, std::string> GetHttpHeaders() const {
    return m_endpoint.get_value().headers_;
  }
  inline EndpointKind GetEndpointKind() const {
    return m_endpoint.get_value().type_;
  }

  inline void SetEndpointKind(EndpointKind kind) {
    m_endpoint.with_mut([kind](Endpoint& ep) { ep.type_ = kind; });
  }

  inline size_t GetMaxTokens() const {
    return m_endpoint.get_value().max_tokens_.value_or(kMaxTokensDefault);
  }

  inline size_t GetContextSize() const {
    return m_endpoint.get_value().context_size_.value_or(kDefaultContextSize);
  }

  inline void SetMaxTokens(size_t count) {
    m_endpoint.with_mut([count](Endpoint& ep) { ep.max_tokens_ = count; });
  }

  inline void SetEndpoint(const Endpoint& ep) {
    m_endpoint.with_mut([ep](Endpoint& endpoint) { endpoint = ep; });
  }

  inline std::string GetModel() const { return m_endpoint.get_value().model_; }

  inline std::optional<Pricing> GetPricing() const {
    std::optional<Pricing> cost;
    m_cost.with([&cost](const std::optional<Pricing>& c) { cost = c; });
    return cost;
  }

  inline void SetPricing(const Pricing& cost) {
    m_cost.with_mut([&cost](std::optional<Pricing>& c) { c = cost; });
  }

  inline void SetLastRequestCost(double cost) {
    m_last_request_amount = cost;
    m_total_amount += cost;
  }

  inline double GetLastRequestCost() const { return m_last_request_amount; }
  inline double GetTotalCost() const { return m_total_amount; }
  inline void ResetCost() {
    m_total_amount = 0;
    m_last_request_amount = 0;
  }

 protected:
  static bool OnResponse(const assistant::response& resp, void* user_data);
  static bool OnResponseRaw(const std::string& resp, void* user_data);
  void ProcessChatRequestQueue();
  bool HandleResponse(const assistant::response& resp,
                      ChatContext& chat_user_data);
  void AddMessage(std::optional<assistant::message> msg);
  virtual assistant::messages GetMessages() const;
  bool ModelHasCapability(const std::string& model_name, ModelCapabilities c);

  FunctionTable m_function_table;
  ChatRequestQueue m_queue;
  Locker<Endpoint> m_endpoint;
  std::atomic_size_t m_windows_size{50};
  /// Messages that were sent to the AI, will be placed here
  Locker<assistant::messages> m_messages;
  Locker<assistant::messages> m_system_messages;
  Locker<ServerTimeout> m_server_timeout;
  Locker<std::unordered_map<std::string, ModelCapabilities>>
      m_model_capabilities;
  std::atomic_bool m_interrupt{false};
  std::atomic_bool m_stream{true};
  Locker<std::string> m_keep_alive{"5m"};
  OnToolInvokeCallback m_on_invoke_tool_cb{nullptr};
  Locker<std::optional<Pricing>> m_cost;
  std::atomic<double> m_total_amount{0.0};
  std::atomic<double> m_last_request_amount{0.0};
  friend struct ChatRequest;
};
}  // namespace assistant
