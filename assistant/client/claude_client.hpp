#pragma once

#include "assistant/claude_response_parser.hpp"
#include "assistant/client/ollama_client.hpp"

namespace assistant {
class ClaudeClient : public OllamaClient {
 public:
  ClaudeClient(const Endpoint& endpoint = AnthropicEndpoint{});
  ~ClaudeClient() override = default;

  ///===--------------------------------------
  /// Override Ollama's behavior with Claude's
  ///===--------------------------------------

  std::optional<json> GetModelInfo(const std::string& model) override;
  /// Return a bitwise operator model capabilities.
  std::optional<ModelCapabilities> GetModelCapabilities(
      const std::string& model) override;

  /// Returns the configured HTTP headers, augmented with the
  /// `anthropic-beta: compact-2026-01-12` header when server-side
  /// compaction is enabled in the active endpoint.
  std::unordered_map<std::string, std::string> GetHttpHeaders() const override;

  void CreateAndPushChatRequest(
      std::optional<assistant::message> msg, OnResponseCallback cb,
      std::string model, ChatOptions chat_options,
      std::shared_ptr<ChatRequestFinaliser> finaliser) override;

  void AddToolsResult(
      std::vector<std::pair<FunctionCall, FunctionResult>> result) override;
  void Compact(size_t responses_to_keep = 3) override;

 protected:
  void ProcessChatRequest(std::shared_ptr<ChatRequest> chat_request) override;
  void ProcessChatRequestQueue() override;

  // Claude does not support system messages as normal messages with a role of
  // "system"
  assistant::messages GetMessages() const override;

  static bool OnRawResponse(const std::string& resp, void* user_data);
  bool HandleResponse(const std::string& resp, ChatContext* chat_context);
  std::shared_ptr<claude::ResponseParser> m_responseParser{nullptr};
};
}  // namespace assistant
