#pragma once

#include "assistant/client/ollama_client.hpp"

namespace assistant {

class OpenAIClient : public OllamaClient {
 public:
  OpenAIClient(const Endpoint& ep = OpenAIEndpoint{});
  ~OpenAIClient() override = default;

  std::optional<ModelCapabilities> GetModelCapabilities(
      [[maybe_unused]] const std::string& model) override;
};

}  // namespace assistant