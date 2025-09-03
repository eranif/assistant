#pragma once

#include <functional>
#include <thread>

#include "macros.hpp"
#include "ollama/function.hpp"
#include "ollama/function_base.hpp"
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
};

enum class ModelCapabilities {
  kNone = (0),
  kThinking = (1 << 0),
  kTooling = (1 << 1),
  kCompletion = (1 << 2),
  kInsert = (1 << 3),
};

ENUM_CLASS_BITWISE_OPERATOS(ModelCapabilities);

using OnResponseCallback = std::function<void(std::string, Reason)>;

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
  static Manager& GetInstance();
  void SetUrl(const std::string& url);

  /// Clear the current session.
  void Reset();

  /// Start a chat. All responses will be directed to the `cb`. Note that `cb`
  /// might get called from a different thread.
  void AsyncChat(std::string msg, OnResponseCallback cb,
                 std::string model = kDefaultModel);
  void SetFunctionTable(FunctionTable table) {
    m_function_table = std::move(table);
  }

  /// Return true if ollama server is running.
  inline bool IsRunning() const { return ollama::is_running(); }

  /// Return list of models available.
  std::vector<std::string> List() const;

  /// Return list of models available using JSON format.
  json ListJSON() const;

  /// Pull model from Ollama registry
  void AsyncPullModel(const std::string& name, OnResponseCallback cb);

  std::optional<json> GetModelInfo(const std::string& model) const;
  /// Return a bitwise operator model capabilities.
  std::optional<ModelCapabilities> GetModelCapabilities(
      const std::string& model) const;
  /// Return model capabilities as array of strings.
  std::optional<std::vector<std::string>> GetModelCapabilitiesString(
      const std::string& model) const;

  /// Set the number of messages to keep when chatting with the model. The
  /// implementation uses a FIFO.
  inline void SetHistorySize(size_t count) { m_windows_size = count; }
  inline size_t GetHistorySize() const { return m_windows_size; }

  /// Define the context size, default is 32K.
  inline void SetContextSize(size_t context_size) {
    m_context_size = context_size;
  }
  inline size_t GetContextSize() const { return m_context_size; }

  void Shutdown();
  void Startup();

 private:
  static bool OnResponse(const ollama::response& resp);
  static void WorkerMain();
  void ProcessContext(std::shared_ptr<ChatContext> context);
  void CreateAndPushContext(std::optional<ollama::message> msg,
                            OnResponseCallback cb, std::string model);
  void AddMessage(std::optional<ollama::message> msg);
  ollama::messages GetMessages() const;

  Manager();
  ~Manager();

  FunctionTable m_function_table;
  ChatContextQueue m_queue;
  std::shared_ptr<std::thread> m_worker;
  std::shared_ptr<std::thread> m_puller_thread;
  std::atomic_bool m_shutdown_flag{false};
  std::atomic_bool m_puller_busy{false};
  std::string m_url;
  size_t m_windows_size{20};
  size_t m_context_size{32 * 1024};
  /// Messages that were sent to the AI, will be placed here
  ollama::messages m_messages;
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
