#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ollama/attributes.hpp"
#include "ollama/config.hpp"

namespace assistant {

constexpr std::string_view kDefaultOllamaUrl = "http://127.0.0.1:11434";
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
};

enum class ModelCapabilities {
  kNone = (0),
  kThinking = (1 << 0),
  kTooling = (1 << 1),
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

using OnResponseCallback = std::function<bool(std::string, Reason, bool)>;

class ClientBase;
struct ChatRequest {
  OnResponseCallback callback_;
  assistant::request request_;
  std::string model_;
  /// If a tool(s) invocation is required, it will be placed here. Once we
  /// invoke the tool and push the tool response + the request to the history
  /// and remove it from here.
  std::vector<std::pair<assistant::message, std::vector<FunctionCall>>>
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

class ClientBase {
 public:
  ClientBase() = default;
  virtual ~ClientBase() { Shutdown(); }

  ///===---------------------------
  /// Client API - START
  ///===---------------------------

  /// The underlying function that triggers the chat.
  virtual void ChatImpl(
      assistant::request& request,
      std::function<bool(const assistant::response& resp, void* user_data)>
          on_response,
      void* user_data) = 0;

  /// Return true if ollama server is running.
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

  ///===---------------------------
  /// Client API - END
  ///===---------------------------

  /// Start a chat. Some options of the chat can be controlled via the
  /// ChatOptions flags. For example, use may disable "tools" even though the
  /// model support them.
  virtual void Chat(std::string msg, OnResponseCallback cb, std::string model,
                    ChatOptions chat_options  // bitwise OR'ed ChatOptions
  );

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

  inline std::string GetUrl() const { return m_url.get_value(); }

 protected:
  static bool OnResponse(const assistant::response& resp, void* user_data);
  void ProcessChatRequestQueue();
  bool HandleResponse(const assistant::response& resp,
                      ChatContext& chat_user_data);
  void ProcessChatRquest(std::shared_ptr<ChatRequest> chat_request);
  void CreateAndPushChatRequest(std::optional<assistant::message> msg,
                                OnResponseCallback cb, std::string model,
                                ChatOptions chat_options);
  void AddMessage(std::optional<assistant::message> msg);
  assistant::messages GetMessages() const;
  bool ModelHasCapability(const std::string& model_name, ModelCapabilities c);

  FunctionTable m_function_table;
  ChatRequestQueue m_queue;
  Locker<std::string> m_url;
  std::atomic_size_t m_windows_size{20};
  /// Messages that were sent to the AI, will be placed here
  Locker<assistant::messages> m_messages;
  Locker<assistant::messages> m_system_messages;
  Locker<std::unordered_map<std::string, ModelOptions>> m_model_options;
  Locker<std::unordered_map<std::string, std::string>> m_http_headers;
  Locker<ModelOptions> m_default_model_options;
  Locker<ServerTimeout> m_server_timeout;
  Locker<std::unordered_map<std::string, ModelCapabilities>>
      m_model_capabilities;
  std::atomic_bool m_interrupt{false};
  std::atomic_bool m_stream{true};
  Locker<std::string> m_keep_alive{"5m"};
  friend struct ChatRequest;
};
}  // namespace assistant
