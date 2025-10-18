#pragma once

#include "assistant/claude_client.hpp"
#include "assistant/claude_response_parser.hpp"
#include "assistant/config.hpp"
#include "assistant/function.hpp"
#include "assistant/ollama_client.hpp"
#include "assistant/tool.hpp"

namespace assistant {

inline std::optional<std::shared_ptr<ClientBase>> MakeClient(
    std::optional<Config> conf) {
  std::shared_ptr<Endpoint> endpoint{nullptr};
  if (conf.has_value()) {
    endpoint = conf.value().GetEndpoint();
  } else {
    endpoint = std::make_shared<Endpoint>();
    endpoint->url_ = kDefaultOllamaUrl;
    endpoint->headers_ = kDefaultOllamaHeaders;
  }

  std::shared_ptr<ClientBase> client{nullptr};
  switch (endpoint->type_) {
    case EndpointKind::ollama:
      client = std::make_shared<OllamaClient>(*endpoint);
      break;
    case EndpointKind::claude:
      client = std::make_shared<ClaudeClient>(*endpoint);
      break;
  }
  if (conf.has_value()) {
    client->ApplyConfig(&conf.value());
  }
  return client;
}

inline std::optional<std::shared_ptr<ClientBase>> MakeClient(
    const std::string& config_content) {
  ASSIGN_OPT_OR_RETURN_NULLOPT(Config conf,
                               Config::FromContent(config_content));
  return MakeClient(conf);
}

inline std::optional<std::shared_ptr<ClientBase>> MakeClient(
    const std::filesystem::path& path) {
  ASSIGN_OPT_OR_RETURN_NULLOPT(Config conf, Config::FromFile(path.string()));
  return MakeClient(conf);
}

}  // namespace assistant
