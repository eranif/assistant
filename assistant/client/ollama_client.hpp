#pragma once

#include <unordered_map>

#include "assistant/client/client_base.hpp"

namespace assistant {

enum class EventType {
  kShutdown,
  kServerReloadConfig,
};

class OllamaClient : public ClientBase {
 public:
  OllamaClient(const Endpoint& ep = OllamaLocalEndpoint{});
  ~OllamaClient() override;

  ///===---------------------------------
  /// Client interface implementation
  ///===---------------------------------

  /// Load configuration object into the manager.
  void ApplyConfig(const assistant::Config* conf) override;

  /// Return true if the server is running.
  bool IsRunning() override;

  /// Return if the server is busy processing a request.
  inline bool IsBusy() const {
    std::scoped_lock lk{m_client_impl_ptr_mutex};
    return m_client_impl_ptr != nullptr;
  }

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

  void AddToolsResult(
      std::vector<std::pair<FunctionCall, FunctionResult>> result) override;

  /// This method should be called from another thread.
  void Interrupt() override;

  void Chat(std::string msg, OnResponseCallback cb,
            ChatOptions chat_options) override;

  void CreateAndPushChatRequest(
      std::optional<assistant::message> msg, OnResponseCallback cb,
      std::string model, ChatOptions chat_options,
      std::shared_ptr<ChatRequestFinaliser> finaliser) override;

  ///===---------------------------------------
  /// Client interface implementation ends here.
  ///===---------------------------------------

 protected:
  virtual void ProcessChatRquest(std::shared_ptr<ChatRequest> chat_request);
  virtual void ProcessChatRequestQueue();
  void SetClientForInterrupt(ITransport* c) {
    std::scoped_lock lk{m_client_impl_ptr_mutex};
    m_client_impl_ptr = c;
  }
  virtual std::unique_ptr<ITransport> CreateClient();

  std::optional<ModelCapabilities> GetOllamaModelCapabilities(
      const std::string& model);

  mutable std::mutex m_client_impl_ptr_mutex;
  ITransport* m_client_impl_ptr GUARDED_BY(m_client_impl_ptr_mutex) = nullptr;
  friend class ClaudeClient;
  friend struct SetInterruptClientLocker;
};

struct SetInterruptClientLocker final {
  OllamaClient* m_ollama_client{nullptr};
  explicit SetInterruptClientLocker(OllamaClient* ollama_client,
                                    ITransport* ptr)
      : m_ollama_client{ollama_client} {
    m_ollama_client->SetClientForInterrupt(ptr);
  }
  ~SetInterruptClientLocker() {
    m_ollama_client->SetClientForInterrupt(nullptr);
  }
};

}  // namespace assistant
