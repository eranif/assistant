#pragma once

#include "assistant/ollama_client.hpp"

namespace assistant {
class ClaudeClient : public OllamaClient {
 public:
  ClaudeClient(
      const std::string& url = "https://api.anthropic.com",
      const std::unordered_map<std::string, std::string>& headers = {});
  ~ClaudeClient() override = default;

  ///===--------------------------------------
  /// Override Ollama's behavior with Claude's
  ///===--------------------------------------

  /// Pull model from Ollama registry
  void PullModel(const std::string& name, OnResponseCallback cb) override;

  std::optional<json> GetModelInfo(const std::string& model) override;
  /// Return a bitwise operator model capabilities.
  std::optional<ModelCapabilities> GetModelCapabilities(
      const std::string& model) override;

  void Chat(std::string msg, OnResponseCallback cb, std::string model,
            ChatOptions chat_options) override;

  void CreateAndPushChatRequest(std::optional<assistant::message> msg,
                                OnResponseCallback cb, std::string model,
                                ChatOptions chat_options) override;

 protected:
  void ProcessChatRquest(std::shared_ptr<ChatRequest> chat_request) override;
  void ProcessChatRequestQueue() override;
  static bool OnRawResponse(const std::string& resp, void* user_data);
};
}  // namespace assistant
