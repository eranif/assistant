#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "assistant/attributes.hpp"
#include "assistant/common.hpp"
#include "assistant/config.hpp"

namespace assistant {

constexpr std::string_view kAssistantRole = "assistant";

class ClientBase;
class ChatRequestFinaliser {
 public:
  ChatRequestFinaliser(std::function<void()> cb)
      : finalise_callback_{std::move(cb)} {}
  ~ChatRequestFinaliser() {
    if (finalise_callback_) {
      finalise_callback_();
    }
  }

 private:
  std::function<void()> finalise_callback_{nullptr};
};

struct ChatRequest {
  OnResponseCallback callback_;
  assistant::request request_;
  std::string model_;
  std::shared_ptr<ChatRequestFinaliser> finaliser_{nullptr};

  /// If a tool(s) invocation is required, it will be placed here. Once we
  /// invoke the tool and push the tool response + the request to the history
  /// and remove it from here.
  std::vector<
      std::pair<std::optional<assistant::message>, std::vector<FunctionCall>>>
      func_calls_;
  void InvokeTools(ClientBase* client,
                   std::shared_ptr<ChatRequestFinaliser> finaliser);
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

struct History {
  /**
   * @brief Constructs a History object with the active history pointing to the
   * main message store.
   *
   * Initializes the swap counter to zero and sets the history pointer to
   * reference the main messages container. The initialization is performed
   * under lock protection.
   */
  History() {
    std::scoped_lock lock{mutex_};
    active_history_ = &messages_;
    swap_count_ = 0;
  }

  /**
   * @brief Destroys the History object.
   *
   * Default destructor that performs standard cleanup of all member variables.
   */
  ~History() = default;

  /**
   * @brief Swaps the active history to the temporary message store.
   *
   * On the first call (when swap_count_ is 0), switches the active history
   * pointer to temp_messages_. Subsequent calls increment the swap counter
   * without changing the pointer, supporting nested swap operations.
   * Thread-safe.
   */
  void SwapToTempHistory() {
    std::scoped_lock lock{mutex_};
    if (swap_count_ == 0) {
      active_history_ = &temp_messages_;
    }
    swap_count_++;
  }

  /**
   * @brief Swaps the active history back to the main message store.
   *
   * Decrements the swap counter and switches back to the main messages_ store
   * only when the counter reaches zero, properly handling nested swaps. Does
   * nothing if already at the main history (swap_count_ is 0). Thread-safe.
   */
  void SwapToMainHistory() {
    std::scoped_lock lock{mutex_};
    if (swap_count_ == 0) {
      return;
    }
    if (swap_count_ == 1) {
      active_history_ = &messages_;
    }
    --swap_count_;
  }

  /**
   * @brief Checks if the active history is the temporary message store.
   *
   * @return true if currently pointing to temp_messages_, false otherwise.
   */
  inline bool IsTempHistory() const {
    std::scoped_lock lock{mutex_};
    return active_history_ == &temp_messages_;
  }

  /**
   * @brief Returns the current swap depth counter.
   *
   * @return The number of unmatched SwapToTempHistory() calls (swap nesting
   * level).
   */
  inline size_t GetSwapCount() const {
    std::scoped_lock lock{mutex_};
    return swap_count_;
  }

  /**
   * @brief Adds a message to the currently active history.
   *
   * @param msg The message to add (moved into the container).
   */
  void AddMessage(assistant::message msg) {
    std::scoped_lock lock{mutex_};
    active_history_->push_back(std::move(msg));
  }

  /**
   * @brief Adds a message to the currently active history if the optional
   * contains a value.
   *
   * @param msg Optional message to add. If empty, the function returns without
   * modification.
   */
  void AddMessage(std::optional<assistant::message> msg) {
    if (!msg.has_value()) {
      return;
    }
    std::scoped_lock lock{mutex_};
    active_history_->push_back(std::move(msg.value()));
  }

  /**
   * @brief Retrieves a copy of all messages in the currently active history.
   *
   * @return A copy of the active message container. Thread-safe.
   */
  assistant::messages GetMessages() const {
    std::scoped_lock lock{mutex_};
    return *active_history_;
  }

  /**
   * @brief Replaces all messages in the currently active history with the
   * provided messages.
   *
   * @param msgs The messages to set (copied into the active history after
   * clearing it).
   */
  void SetMessages(const assistant::messages& msgs) {
    std::scoped_lock lock{mutex_};
    active_history_->clear();
    active_history_->insert(active_history_->end(), msgs.begin(), msgs.end());
  }

  /**
   * @brief Reduces the active history size to a maximum by removing the oldest
   * messages.
   *
   * Removes messages from the beginning of the active history until the size is
   * at or below max_size. Does nothing if the current size is already within
   * the limit.
   *
   * @param max_size The maximum number of messages to retain.
   */
  void ShrinkToFit(size_t max_size) {
    std::scoped_lock lock{mutex_};
    if (active_history_->size() <= max_size) {
      return;
    }

    // Remove messages from the start ("old messages")
    while (!active_history_->empty() && (active_history_->size() > max_size)) {
      active_history_->erase(active_history_->begin());
    }
  }

  /**
   * @brief Clears all messages from the currently active history.
   *
   * Removes all messages from whichever history is currently active (main or
   * temporary).
   */
  void Clear() {
    std::scoped_lock lock{mutex_};
    active_history_->clear();
  }

  /**
   * @brief Clears all messages from both the main and temporary message stores.
   *
   * Unconditionally clears both messages_ and temp_messages_, regardless of
   * which is active.
   */
  void ClearAll() {
    std::scoped_lock lock{mutex_};
    messages_.clear();
    temp_messages_.clear();
  }

  /**
   * @brief Checks if the currently active history is empty.
   *
   * @return true if the active history contains no messages, false otherwise.
   */
  inline bool IsEmpty() const {
    std::scoped_lock lock{mutex_};
    return active_history_->empty();
  }

 private:
  mutable std::mutex mutex_;
  assistant::messages messages_ GUARDED_BY(mutex_);
  assistant::messages temp_messages_ GUARDED_BY(mutex_);
  assistant::messages* active_history_ GUARDED_BY(mutex_){nullptr};
  size_t swap_count_ GUARDED_BY(mutex_){0};
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

  virtual void CreateAndPushChatRequest(
      std::optional<assistant::message> msg, OnResponseCallback cb,
      std::string model, ChatOptions chat_options,
      std::shared_ptr<ChatRequestFinaliser> finaliser) = 0;

  virtual void AddToolsResult(
      std::vector<std::pair<FunctionCall, FunctionResult>> result) = 0;

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
  void ClearHistoryMessages() { m_history.Clear(); }

  /// Return the history messages.
  std::vector<Message> GetHistory() const {
    std::vector<Message> history;
    auto msgs = m_history.GetMessages();
    for (const auto& msg : msgs) {
      auto message = Message::from_message(msg);
      if (message.has_value()) {
        history.push_back(message.value());
      }
    }
    return history;
  }

  /// Replace the history.
  void SetHistory(const std::vector<Message>& history) {
    assistant::messages m;
    m.reserve(history.size());

    for (const auto& msg : history) {
      m.push_back(msg.as_message());
    }
    m_history.SetMessages(m);
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
    return m_cost.get_value();
  }

  inline void SetPricing(const Pricing& cost) { m_cost.set_value(cost); }

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

  inline std::optional<Usage> GetLastRequestUsage() const {
    return m_last_request_usage.get_value();
  }

  inline void SetLastRequestUsage(const Usage& usage) {
    m_last_request_usage.set_value(usage);
    m_aggregated_usage.with_mut([&usage](Usage& agg) { agg.Add(usage); });
  }

  inline Usage GetAggregatedUsage() const {
    return m_aggregated_usage.get_value();
  }

  inline void SetCachingPolicy(CachePolicy policy) {
    m_caching_policy.set_value(policy);
  }

  inline CachePolicy GetCachingPolicy() const {
    return m_caching_policy.get_value();
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
  std::atomic_size_t m_windows_size{500};
  /// Messages that were sent to the AI, will be placed here
  History m_history;
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
  Locker<std::optional<Usage>> m_last_request_usage;
  Locker<Usage> m_aggregated_usage;
  std::atomic_bool m_multi_tool_reply_as_array{false};
  Locker<CachePolicy> m_caching_policy{CachePolicy::kNone};
  friend struct ChatRequest;
};
}  // namespace assistant
