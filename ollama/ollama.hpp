#pragma once

#include <functional>
#include <thread>
#include <unordered_map>

#include "macros.hpp"
#include "ollama/function.hpp"
#include "ollama/function_base.hpp"
#include "ollama/model_options.hpp"
#include "ollama/ollamalib.hpp"

namespace ollama {

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
};

ENUM_CLASS_BITWISE_OPERATOS(ModelCapabilities);

using OnResponseCallback = std::function<void(std::string, Reason, bool)>;

const std::string kDefaultModel = "qwen2.5:7b";

class Manager;
struct ChatContext {
  OnResponseCallback callback_;
  ollama::request request_;
  std::string model_;
  /// If a tool(s) invocation is required, it will be placed here. Once we
  /// invoke the tool and push the tool response + the request to the history
  /// and remove it from here.
  std::vector<std::pair<ollama::message, std::vector<FunctionCall>>>
      func_calls_;
  void InvokeTools(Manager* manager);
};

/// We pass this struct to provide context in the callback.
struct ChatUserData {
  Manager* manager{nullptr};
  std::string model;
  bool thinking{false};
  bool model_can_think{false};
  std::string thinking_start_tag{"<think>"};
  std::string thinking_end_tag{"</think>"};
};

struct ChatContextQueue {
 public:
  inline std::shared_ptr<ChatContext> Pop() { return Top(true); }
  inline std::shared_ptr<ChatContext> Top() { return Top(false); }
  inline void PushBack(std::shared_ptr<ChatContext> context) {
    std::unique_lock lk{m_mutex};
    m_vec.push_back(context);
  }

  inline void Clear() {
    std::unique_lock lk{m_mutex};
    m_vec.clear();
  }

  inline size_t GetSize() const {
    std::unique_lock lk{m_mutex};
    return m_vec.size();
  }

  inline bool IsEmpty() const { return GetSize() == 0; }

 private:
  inline std::shared_ptr<ChatContext> Top(bool pop_it) {
    std::unique_lock lk{m_mutex};
    if (m_vec.empty()) {
      return nullptr;
    }
    auto p = m_vec.front();
    if (pop_it) {
      m_vec.erase(m_vec.begin());
    }
    return p;
  }

  mutable std::mutex m_mutex;
  std::vector<std::shared_ptr<ChatContext>> m_vec;
};

class Manager {
 public:
  Manager();
  ~Manager();

  static Manager& GetInstance();
  void SetUrl(const std::string& url);
  void SetHeaders(const std::unordered_map<std::string, std::string>& headers);

  /// Add system message to the prompt. System messages are always sent as part
  /// of the prompt
  void AddSystemMessage(const std::string& msg);

  /// Clear all system messages.
  void ClearSystemMessages();

  /// Perform a hard reset - this clears everything:
  /// Function table, history, buffered messages etc.
  void Reset();

  /// Clear the current session.
  void SoftReset();

  /// Start a chat. All responses will be directed to the `cb`. Note that `cb`
  /// might get called from a different thread.
  void AsyncChat(std::string msg, OnResponseCallback cb,
                 std::string model = kDefaultModel);

  /// Load configuration object into the manager.
  void ApplyConfig(const ollama::Config* conf);

  /// Return true if ollama server is running.
  bool IsRunning();

  /// Return list of models available.
  std::vector<std::string> List();

  /// Return list of models available using JSON format.
  json ListJSON();

  /// Pull model from Ollama registry
  void AsyncPullModel(const std::string& name, OnResponseCallback cb);

  std::optional<json> GetModelInfo(const std::string& model);
  /// Return a bitwise operator model capabilities.
  std::optional<ModelCapabilities> GetModelCapabilities(
      const std::string& model);
  /// Return model capabilities as array of strings.
  std::optional<std::vector<std::string>> GetModelCapabilitiesString(
      const std::string& model);

  /// Set the number of messages to keep when chatting with the model. The
  /// implementation uses a FIFO.
  inline void SetHistorySize(size_t count) { m_windows_size = count; }
  inline size_t GetHistorySize() const { return m_windows_size; }

  /// Define the context size, default is 32K.
  inline void SetContextSize(size_t context_size) {
    m_context_size = context_size;
  }
  inline size_t GetContextSize() const { return m_context_size; }
  inline void SetPreferCPU(bool b) { m_preferCPU = b; }
  inline bool GetPreferCPU() const { return m_preferCPU; }
  const FunctionTable& GetFunctionTable() const { return m_function_table; }
  FunctionTable& GetFunctionTable() { return m_function_table; }

  void Shutdown();
  void Startup();
  void Interrupt();

 private:
  static bool OnResponse(const ollama::response& resp, void* user_data);
  static void WorkerMain(Manager* manager);
  bool HandleResponse(const ollama::response& resp,
                      ChatUserData& chat_user_data);
  void ProcessContext(std::shared_ptr<ChatContext> context);
  void CreateAndPushContext(std::optional<ollama::message> msg,
                            OnResponseCallback cb, std::string model);
  void AddMessage(std::optional<ollama::message> msg);
  ollama::messages GetMessages() const;
  bool ModelHasCapability(const std::string& model_name, ModelCapabilities c);

  Ollama m_ollama;
  FunctionTable m_function_table;
  ChatContextQueue m_queue;
  std::shared_ptr<std::thread> m_worker;
  std::shared_ptr<std::thread> m_model_puller_thread;
  std::atomic_bool m_shutdown_flag{false};
  std::atomic_bool m_puller_busy{false};
  std::string m_url;
  size_t m_windows_size{20};
  size_t m_context_size{32 * 1024};
  bool m_preferCPU{false};
  /// Messages that were sent to the AI, will be placed here
  ollama::messages m_messages;
  ollama::messages m_system_messages;
  std::unordered_map<std::string, ModelOptions> m_model_options;
  ModelOptions m_default_model_options;
  std::unordered_map<std::string, ModelCapabilities> m_model_capabilities;
  std::string m_current_response;
  std::atomic_bool m_interrupt{false};
  friend struct ChatContext;
};
}  // namespace ollama

/**
 * @brief Joins the elements of a container into a single string with a
 * separator.
 *
 * @tparam Container A container type (e.g., std::vector, std::list,
 * std::array).
 * @param elements The container of elements to join.
 * @param separator The string used to separate the elements.
 * @return The joined string.
 */
template <typename Container>
std::string JoinArray(const Container& elements, const std::string& separator) {
  // Return an empty string immediately if the container is empty.
  if (elements.empty()) {
    return "";
  }

  std::ostringstream oss;

  // Use an iterator to handle all container types.
  auto it = elements.begin();

  // Append the first element without a separator.
  oss << *it;
  ++it;

  // Append the remaining elements with the separator.
  for (; it != elements.end(); ++it) {
    oss << separator << *it;
  }

  return oss.str();
}

template <typename T>
inline std::ostream& operator<<(std::ostream& o, const std::vector<T>& v) {
  o << JoinArray(v, "\n");
  return o;
}
