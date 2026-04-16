#include <gtest/gtest.h>

#include "assistant/client/openai_messages_client.hpp"
#include "assistant/config.hpp"

using namespace assistant;

TEST(OpenAIMessagesClient, EndpointConfiguration) {
  MoonshotAIEndpoint endpoint;

  // Verify endpoint is configured for OpenAI messages API
  EXPECT_EQ(endpoint.type_, EndpointKind::moonshotai);
  EXPECT_EQ(endpoint.url_, kEndpointOpenAI);
}

TEST(OpenAIMessagesClient, ClientCreation) {
  MoonshotAIEndpoint endpoint;
  endpoint.headers_["Authorization"] = "Bearer test-key";

  OpenAIMessagesClient client(endpoint);

  // Verify the client is created
  EXPECT_TRUE(client.IsStreaming());
}

TEST(OpenAIMessagesClient, ModelCapabilities) {
  MoonshotAIEndpoint endpoint;
  OpenAIMessagesClient client(endpoint);

  auto caps = client.GetModelCapabilities("gpt-4o");
  ASSERT_TRUE(caps.has_value());

  // Verify expected capabilities
  EXPECT_TRUE(IsFlagSet(caps.value(), ModelCapabilities::kTools));
  EXPECT_TRUE(IsFlagSet(caps.value(), ModelCapabilities::kCompletion));
  EXPECT_TRUE(IsFlagSet(caps.value(), ModelCapabilities::kInsert));
  EXPECT_TRUE(IsFlagSet(caps.value(), ModelCapabilities::kThinking));
}

// Note: Actual chat tests would require a valid API key and network access
// These are basic structural tests to verify the client is properly configured
