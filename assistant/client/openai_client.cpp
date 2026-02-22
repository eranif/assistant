#include "assistant/client/openai_client.hpp"

namespace assistant {
OpenAIClient::OpenAIClient(const Endpoint& ep) : OllamaClient(ep) {}

std::optional<ModelCapabilities> OpenAIClient::GetModelCapabilities(
    [[maybe_unused]] const std::string& model) {
  ModelCapabilities flags{ModelCapabilities::kNone};
  AddFlagSet(flags, ModelCapabilities::kTools);
  AddFlagSet(flags, ModelCapabilities::kCompletion);
  AddFlagSet(flags, ModelCapabilities::kInsert);
  AddFlagSet(flags, ModelCapabilities::kThinking);
  return flags;
}
}  // namespace assistant
