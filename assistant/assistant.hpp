#pragma once

#include "assistant/claude_response_parser.hpp"
#include "assistant/client/claude_client.hpp"
#include "assistant/client/ollama_client.hpp"
#include "assistant/client/openai_client.hpp"
#include "assistant/config.hpp"
#include "assistant/function.hpp"
#include "assistant/tool.hpp"

namespace assistant {

inline std::optional<std::shared_ptr<ClientBase>> MakeClient(
    std::optional<Config> conf) {
  std::shared_ptr<Endpoint> endpoint{nullptr};
  if (conf.has_value()) {
    endpoint = conf.value().GetEndpoint();
  }

  if (endpoint == nullptr) {
    return std::nullopt;
  }

  std::shared_ptr<ClientBase> client{nullptr};
  try {
    switch (endpoint->type_) {
      case EndpointKind::ollama:
        client = std::make_shared<OllamaClient>(*endpoint);
        break;
      case EndpointKind::anthropic:
        client = std::make_shared<ClaudeClient>(*endpoint);
        break;
      case EndpointKind::openai:
        client = std::make_shared<OpenAIClient>(*endpoint);
        break;
    }
  } catch (const std::exception& e) {
    OLOG(LogLevel::kError) << "Could not create client. " << e.what();
    return std::nullopt;
  }

  if (conf.has_value()) {
    client->ApplyConfig(&conf.value());
  }
  return client;
}

inline std::optional<std::shared_ptr<ClientBase>> MakeClient(
    const std::string& config_content) {
  auto result = ConfigBuilder::FromContent(config_content);
  if (!result.ok()) {
    return std::nullopt;
  }
  return MakeClient(result.config_.value());
}

inline std::optional<std::shared_ptr<ClientBase>> MakeClient(
    const std::filesystem::path& path) {
  auto result = ConfigBuilder::FromFile(path.string());
  if (!result.ok()) {
    return std::nullopt;
  }
  return MakeClient(result.config_.value());
}

}  // namespace assistant
