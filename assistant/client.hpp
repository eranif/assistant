#pragma once

#include <atomic>
#include <functional>
#include <unordered_map>

#include "assistant/attributes.hpp"
#include "assistant/client_base.hpp"
#include "assistant/thread_notifier.hpp"

namespace assistant {

enum class EventType {
  kShutdown,
  kServerReloadConfig,
};

class OllamaClient : public ClientBase {
 public:
  OllamaClient(const std::string& url,
               const std::unordered_map<std::string, std::string>& headers);
  ~OllamaClient() override;

  ///===---------------------------------
  /// Client interface implementation
  ///===---------------------------------

  /// Load configuration object into the manager.
  void ApplyConfig(const assistant::Config* conf) override;

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

  void Chat(std::string msg, OnResponseCallback cb, std::string model,
            ChatOptions chat_options) override;

  void CreateAndPushChatRequest(std::optional<assistant::message> msg,
                                OnResponseCallback cb, std::string model,
                                ChatOptions chat_options) override;

  ///===---------------------------------------
  /// Client interface implementation ends here.
  ///===---------------------------------------

  /// Timeout for connecting the server.
  void SetConnectTimeout(const int secs, const int usecs) {
    std::scoped_lock lk{m_client_mutex};
    m_client_impl.setConnectTimeout(secs, usecs);
  }

  /// Timeout for reading from the server.
  void SetReadTimeout(const int secs, const int usecs) {
    std::scoped_lock lk{m_client_mutex};
    m_client_impl.setReadTimeout(secs, usecs);
  }

  /// Timeout for writer to the server.
  void SetWriteTimeout(const int secs, const int usecs) {
    std::scoped_lock lk{m_client_mutex};
    m_client_impl.setWriteTimeout(secs, usecs);
  }

 protected:
  virtual void ProcessChatRquest(std::shared_ptr<ChatRequest> chat_request);
  virtual void ProcessChatRequestQueue();

  std::optional<ModelCapabilities> GetOllamaModelCapabilities(
      const std::string& model);
  void SetHeadersInternal(
      ClientImpl& client,
      const std::unordered_map<std::string, std::string>& headers);

  /// Check if the server is running. This method does not rely on the cached
  /// "m_is_running_flag" variable.
  bool IsRunningInternal(ClientImpl& client) const;

  std::mutex m_client_mutex;
  ClientImpl m_client_impl;

  friend class ClaudeClient;
};
}  // namespace assistant
