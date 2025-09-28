#pragma once

#include <atomic>
#include <functional>
#include <unordered_map>

#include "ollama/attributes.hpp"
#include "ollama/client_base.hpp"
#include "ollama/thread_notifier.hpp"

namespace ollama {

enum class EventType {
  kShutdown,
  kServerReloadConfig,
};

class Client : public ClientBase {
 public:
  Client(const std::string& url,
         const std::unordered_map<std::string, std::string>& headers);
  ~Client() override;

  /// Start a chat. All responses will be directed to the `cb`. Note that `cb`
  void ChatImpl(
      ollama::request& request,
      std::function<bool(const ollama::response& resp, void* user_data)>
          on_response,
      void* user_data) override;

  /// Load configuration object into the manager.
  void ApplyConfig(const ollama::Config* conf) override;

  /// Return true if ollama server is running.
  bool IsRunning() override;

  /// Return list of models available.
  std::vector<std::string> List() override;

  /// Return list of models available using JSON format.
  json ListJSON() override;

  /// Pull model from Ollama registry
  void PullModel(const std::string& name, OnResponseCallback cb) override;

  std::optional<json> GetModelInfo(const std::string& model) override;
  /// Return a bitwise operator model capabilities.
  std::optional<ModelCapabilities> GetModelCapabilities(
      const std::string& model) override;

  /// This method should be called from another thread.
  void Interrupt() override;

 private:
  void SetHeadersInternal(
      Ollama& client,
      const std::unordered_map<std::string, std::string>& headers);

  void StartIsAliveThread();
  void StopIsAliveThread();
  void IsAliveThreadMain();

  /// Check if the server is running. This method does not rely on the cached
  /// "m_is_running_flag" variable.
  bool IsRunningInternal(Ollama& client) const;

  std::mutex m_ollama_mutex;
  Ollama m_ollama GUARDED_BY(m_ollama_mutex);
  std::atomic_bool m_is_running_flag{false};
  ThreadNotifier<EventType> m_is_alive_channel;
  std::unique_ptr<std::thread> m_isAliveThread;
};
}  // namespace ollama
