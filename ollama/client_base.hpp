#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ollama/config.hpp"
#include "ollama/helpers.hpp"

namespace ollama {

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
  kDefault = (0),
  kNoTools = (1 << 0),
};

using OnResponseCallback = std::function<void(std::string, Reason, bool)>;

class ClientBase;
struct ChatContext {
  OnResponseCallback callback_;
  ollama::request request_;
  std::string model_;
  /// If a tool(s) invocation is required, it will be placed here. Once we
  /// invoke the tool and push the tool response + the request to the history
  /// and remove it from here.
  std::vector<std::pair<ollama::message, std::vector<FunctionCall>>>
      func_calls_;
  void InvokeTools(ClientBase* client);
};

/// We pass this struct to provide context in the callback.
struct ChatUserData {
  ClientBase* client{nullptr};
  std::string model;
  bool thinking{false};
  bool model_can_think{false};
  std::shared_ptr<ChatContext> chat_context{nullptr};
  std::string thinking_start_tag{"<think>"};
  std::string thinking_end_tag{"</think>"};
};

struct ChatContextQueue : public std::vector<std::shared_ptr<ChatContext>> {
 public:
  ChatContextQueue() = default;
  ~ChatContextQueue() = default;

  inline std::shared_ptr<ChatContext> pop_front_and_return() {
    if (empty()) {
      return nullptr;
    }
    auto fr = front();
    erase(begin());
    return fr;
  }
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
      ollama::request& request,
      std::function<bool(const ollama::response& resp, void* user_data)>
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

  virtual void ApplyConfig(const ollama::Config* conf);
  virtual void Startup() { m_interrupt = false; }
  virtual void Shutdown() { m_queue.clear(); }
  virtual void Interrupt() { m_interrupt = true; }

  /// Perform a hard reset - this clears everything:
  /// Function table, history, buffered messages etc.
  virtual void Reset() {
    SoftReset();
    m_function_table.Clear();
    ClearSystemMessages();
    m_interrupt = false;
  }

  /// Clear the current session.
  virtual void SoftReset() {
    m_queue.clear();
    m_messages.clear();
    m_current_response.clear();
    m_interrupt = false;
  }

  /// Set the number of messages to keep when chatting with the model. The
  /// implementation uses a FIFO.
  inline void SetHistorySize(size_t count) { m_windows_size = count; }
  inline size_t GetHistorySize() const { return m_windows_size; }

  const FunctionTable& GetFunctionTable() const { return m_function_table; }
  FunctionTable& GetFunctionTable() { return m_function_table; }

  /// Add system message to the prompt. System messages are always sent as part
  /// of the prompt
  void AddSystemMessage(const std::string& msg);

  /// Clear all system messages.
  void ClearSystemMessages();

  inline const std::string& GetUrl() const { return m_url; }

 protected:
  static bool OnResponse(const ollama::response& resp, void* user_data);
  void ProcessQueue();
  bool HandleResponse(const ollama::response& resp,
                      ChatUserData& chat_user_data);
  void ProcessContext(std::shared_ptr<ChatContext> context);
  void CreateAndPushContext(std::optional<ollama::message> msg,
                            OnResponseCallback cb, std::string model,
                            ChatOptions chat_options);
  void AddMessage(std::optional<ollama::message> msg);
  ollama::messages GetMessages() const;
  bool ModelHasCapability(const std::string& model_name, ModelCapabilities c);

  FunctionTable m_function_table;
  ChatContextQueue m_queue;
  std::string m_url;
  size_t m_windows_size{20};
  /// Messages that were sent to the AI, will be placed here
  ollama::messages m_messages;
  ollama::messages m_system_messages;
  std::unordered_map<std::string, ModelOptions> m_model_options;
  std::unordered_map<std::string, std::string> m_http_headers;
  ModelOptions m_default_model_options;
  std::unordered_map<std::string, ModelCapabilities> m_model_capabilities;
  std::string m_current_response;
  std::atomic_bool m_interrupt{false};
  friend struct ChatContext;
};
}  // namespace ollama