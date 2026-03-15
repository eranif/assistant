#pragma once

#include "assistant/client/ollama_client.hpp"
#include "assistant/openai_response_parser.hpp"

namespace assistant {

class OpenAIClient : public OllamaClient {
 public:
  OpenAIClient(const Endpoint& ep = OpenAIEndpoint{});
  ~OpenAIClient() override = default;

  std::optional<ModelCapabilities> GetModelCapabilities(
      [[maybe_unused]] const std::string& model) override;
  static bool OnRawResponse(const std::string& resp, void* user_data);

 protected:
  void ProcessChatRquest(std::shared_ptr<ChatRequest> chat_request) override;
  bool HandleResponse(const std::string& resp, ChatContext* chat_context);

  std::unique_ptr<OpenAIResponseParser> m_responseParser;
};

}  // namespace assistant
