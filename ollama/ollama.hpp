#pragma once

#include <functional>
#include <thread>

#include "ollamalib.hpp"
#include "tool.hpp"

namespace ollama {
using FunctionTable = tool::FunctionTable;
using FunctionCall = tool::FunctionCall;

enum class Reason {
  /// The current reason completed successfully.
  kDone,
  /// More data to come.
  kPartialResult,
  /// A non recoverable error.
  kFatalError
};
using OnResponseCallback = std::function<void(std::string, Reason)>;

const std::string kDefaultModel = "qwen2.5:7b";

class Manager;
struct ChatContext {
  OnResponseCallback done_cb_;
  ollama::request request_;
  /// Messages that were sent to the AI, will be placed here
  ollama::messages history_;
  std::string model_;
  /// If a tool(s) invocation is required, it will be placed here. Once we
  /// invoke the tool and push the tool response + the request to the history
  /// and remove it from here.
  std::vector<std::pair<ollama::message, std::vector<tool::FunctionCall>>>
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
  void SetUrl(std::string_view url);
  void Reset();

  /// Start a chat. All responses will be directed to the `cb`. Note that `cb`
  /// might get called from a different thread.
  void AsyncChat(std::string msg, OnResponseCallback cb,
                 std::string model = kDefaultModel);

  void SetFunctionTable(FunctionTable table) {
    m_functionTable = std::move(table);
  }
  void Shutdown();
  void Startup();

 private:
  static bool OnResponse(const ollama::response& resp);
  static void WorkerMain();
  void ProcessContext(std::shared_ptr<ChatContext> context);
  Manager();
  ~Manager();

  FunctionTable m_functionTable;
  ChatContextQueue m_queue;
  std::shared_ptr<std::thread> m_worker;
  std::atomic_bool m_shutdown_flag{false};
  friend class ChatContext;
};
}  // namespace ollama
