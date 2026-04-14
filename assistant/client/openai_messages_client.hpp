#pragma once

#include "assistant/client/ollama_client.hpp"
#include "assistant/chat_completions_response_parser.hpp"

namespace assistant {

/**
 * OpenAI client that uses the /v1/chat/completions endpoint (Messages API)
 * instead of the /v1/responses endpoint.
 *
 * This is the standard OpenAI Chat Completions API used by most applications.
 */
class OpenAIMessagesClient : public OllamaClient {
 public:
  OpenAIMessagesClient(const Endpoint& ep = OpenAIMessagesEndpoint{});
  ~OpenAIMessagesClient() override = default;

  std::optional<ModelCapabilities> GetModelCapabilities(
      [[maybe_unused]] const std::string& model) override;
  void AddToolsResult(
      std::vector<std::pair<FunctionCall, FunctionResult>> result) override;

  /// Only streaming is supported with OpenAI
  inline bool IsStreaming() const override { return true; }

 protected:
  static bool OnRawResponse(const std::string& resp, void* user_data);
  void ProcessChatRequest(std::shared_ptr<ChatRequest> chat_request) override;
  virtual bool HandleResponse(const std::string& resp, ChatContext* chat_context);

  std::unique_ptr<chat_completions::ResponseParser> m_responseParser;
};

}  // namespace assistant
