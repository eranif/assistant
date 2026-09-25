#include <gtest/gtest.h>

#include "assistant/assistantlib.hpp"
#include "assistant/client/openai_client.hpp"
#include "assistant/config.hpp"

using namespace assistant;

// Test OpenAI endpoint configuration
TEST(OpenAIClientTest, EndpointConfiguration) {
  OpenAIEndpoint endpoint;
  EXPECT_EQ(endpoint.url_, kEndpointOpenAI);
  EXPECT_EQ(endpoint.type_, EndpointKind::openai);
  EXPECT_EQ(endpoint.url_, "https://api.openai.com");
}

// Test OpenAI client creation with default endpoint
TEST(OpenAIClientTest, ClientCreation) {
  OpenAIEndpoint endpoint;
  endpoint.model_ = "gpt-4";

  OpenAIClient client(endpoint);

  EXPECT_EQ(client.GetUrl(), "https://api.openai.com");
  EXPECT_EQ(client.GetEndpointKind(), EndpointKind::openai);
  EXPECT_EQ(client.GetModel(), "gpt-4");
}

// Test OpenAI client with custom endpoint (for testing with local proxy)
TEST(OpenAIClientTest, CustomEndpoint) {
  OpenAIEndpoint endpoint;
  endpoint.url_ = "http://localhost:8080";
  endpoint.model_ = "gpt-3.5-turbo";

  OpenAIClient client(endpoint);

  EXPECT_EQ(client.GetUrl(), "http://localhost:8080");
  EXPECT_EQ(client.GetModel(), "gpt-3.5-turbo");
}

// Test model capabilities
TEST(OpenAIClientTest, ModelCapabilities) {
  OpenAIClient client;

  auto capabilities = client.GetModelCapabilities("gpt-4");
  ASSERT_TRUE(capabilities.has_value());

  auto caps = capabilities.value();
  EXPECT_TRUE(IsFlagSet(caps, ModelCapabilities::kTools));
  EXPECT_TRUE(IsFlagSet(caps, ModelCapabilities::kCompletion));
  EXPECT_TRUE(IsFlagSet(caps, ModelCapabilities::kInsert));
  EXPECT_TRUE(IsFlagSet(caps, ModelCapabilities::kThinking));
}

// Test different model capabilities
TEST(OpenAIClientTest, ModelCapabilitiesForDifferentModels) {
  OpenAIClient client;

  // Test GPT-4
  auto gpt4_caps = client.GetModelCapabilities("gpt-4");
  ASSERT_TRUE(gpt4_caps.has_value());
  EXPECT_TRUE(IsFlagSet(gpt4_caps.value(), ModelCapabilities::kTools));

  // Test GPT-3.5
  auto gpt35_caps = client.GetModelCapabilities("gpt-3.5-turbo");
  ASSERT_TRUE(gpt35_caps.has_value());
  EXPECT_TRUE(IsFlagSet(gpt35_caps.value(), ModelCapabilities::kCompletion));
}

// Test endpoint headers configuration
TEST(OpenAIClientTest, EndpointHeaders) {
  OpenAIEndpoint endpoint;
  endpoint.headers_["Authorization"] = "Bearer sk-test-key";
  endpoint.headers_["Content-Type"] = "application/json";

  OpenAIClient client(endpoint);

  auto headers = client.GetHttpHeaders();
  EXPECT_EQ(headers.size(), 2);
  EXPECT_EQ(headers["Authorization"], "Bearer sk-test-key");
  EXPECT_EQ(headers["Content-Type"], "application/json");
}

// Test max tokens configuration
TEST(OpenAIClientTest, MaxTokensConfiguration) {
  OpenAIEndpoint endpoint;
  endpoint.max_tokens_ = 2048;

  OpenAIClient client(endpoint);

  EXPECT_EQ(client.GetMaxTokens(), 2048);
}

// Test context size configuration
TEST(OpenAIClientTest, ContextSizeConfiguration) {
  OpenAIEndpoint endpoint;
  endpoint.context_size_ = 8192;

  OpenAIClient client(endpoint);

  EXPECT_EQ(client.GetContextSize(), 8192);
}

// Test system messages
TEST(OpenAIClientTest, SystemMessages) {
  OpenAIClient client;

  client.AddSystemMessage("You are a helpful assistant.");
  client.AddSystemMessage("Always respond in JSON format.");

  client.ClearSystemMessages();
  // After clearing, system messages should be empty
  // (we can't directly test this without accessing internals,
  // but we verify the operations don't crash)
}

// Test history management
TEST(OpenAIClientTest, HistoryManagement) {
  OpenAIClient client;

  assistant::messages history;
  history.push_back(assistant::message{"user", "Hello"});
  history.push_back(assistant::message{"assistant", "Hi there!"});

  client.SetHistory(history);
  auto retrieved = client.GetHistory();

  ASSERT_EQ(retrieved.size(), 2);
  EXPECT_EQ(retrieved[0]["role"].get<std::string>(), "user");
  EXPECT_EQ(retrieved[0]["content"].get<std::string>(), "Hello");
  EXPECT_EQ(retrieved[1]["role"].get<std::string>(), "assistant");
  EXPECT_EQ(retrieved[1]["content"].get<std::string>(), "Hi there!");

  client.ClearHistoryMessages();
  retrieved = client.GetHistory();
  EXPECT_EQ(retrieved.size(), 0);
}

// Test pricing configuration
TEST(OpenAIClientTest, PricingConfiguration) {
  OpenAIClient client;

  Pricing pricing;
  pricing.input_tokens = 0.01;
  pricing.output_tokens = 0.03;

  client.SetPricing(pricing);

  auto retrieved_pricing = client.GetPricing();
  ASSERT_TRUE(retrieved_pricing.has_value());
  EXPECT_DOUBLE_EQ(retrieved_pricing->input_tokens, 0.01);
  EXPECT_DOUBLE_EQ(retrieved_pricing->output_tokens, 0.03);
}

// Test cost tracking
TEST(OpenAIClientTest, CostTracking) {
  OpenAIClient client;

  // Initial cost should be 0
  EXPECT_DOUBLE_EQ(client.GetTotalCost(), 0.0);
  EXPECT_DOUBLE_EQ(client.GetLastRequestCost(), 0.0);

  // Simulate request costs
  client.SetLastRequestCost(0.05);
  EXPECT_DOUBLE_EQ(client.GetLastRequestCost(), 0.05);
  EXPECT_DOUBLE_EQ(client.GetTotalCost(), 0.05);

  client.SetLastRequestCost(0.03);
  EXPECT_DOUBLE_EQ(client.GetLastRequestCost(), 0.03);
  EXPECT_DOUBLE_EQ(client.GetTotalCost(), 0.08);

  // Reset cost
  client.ResetCost();
  EXPECT_DOUBLE_EQ(client.GetTotalCost(), 0.0);
  EXPECT_DOUBLE_EQ(client.GetLastRequestCost(), 0.0);
}

// Test usage tracking
TEST(OpenAIClientTest, UsageTracking) {
  OpenAIClient client;

  Usage usage1;
  usage1.input_tokens = 100;
  usage1.output_tokens = 50;

  client.SetLastRequestUsage(usage1);

  auto last_usage = client.GetLastRequestUsage();
  ASSERT_TRUE(last_usage.has_value());
  EXPECT_EQ(last_usage->input_tokens, 100);
  EXPECT_EQ(last_usage->output_tokens, 50);

  auto aggregated = client.GetAggregatedUsage();
  EXPECT_EQ(aggregated.input_tokens, 100);
  EXPECT_EQ(aggregated.output_tokens, 50);

  Usage usage2;
  usage2.input_tokens = 200;
  usage2.output_tokens = 75;

  client.SetLastRequestUsage(usage2);

  aggregated = client.GetAggregatedUsage();
  EXPECT_EQ(aggregated.input_tokens, 300);
  EXPECT_EQ(aggregated.output_tokens, 125);
}

namespace {
// Exposes the (protected) history so tests can switch to the temp history.
class TestableOpenAIClient : public OpenAIClient {
 public:
  using OpenAIClient::OpenAIClient;
  void SwapToTemp() { m_history.SwapToTempHistory(); }
  void SwapToMain() { m_history.SwapToMainHistory(); }
};
}  // namespace

// Requests made against the temp history must not affect the context usage of
// the main conversation, but must still be counted in the aggregated usage.
TEST(OpenAIClientTest, TempHistoryUsageDoesNotAffectMainContext) {
  OpenAIEndpoint endpoint;
  endpoint.context_size_ = 1000;
  TestableOpenAIClient client(endpoint);

  Usage main_usage;
  main_usage.input_tokens = 100;
  main_usage.output_tokens = 10;
  client.SetLastRequestUsage(main_usage);

  client.SwapToTemp();
  Usage temp_usage;
  temp_usage.input_tokens = 900;
  temp_usage.output_tokens = 50;
  client.SetLastRequestUsage(temp_usage);
  client.SwapToMain();

  // Last usage still reflects the main conversation
  auto last = client.GetLastRequestUsage();
  ASSERT_TRUE(last.has_value());
  EXPECT_EQ(last->input_tokens, 100);
  EXPECT_EQ(last->output_tokens, 10);

  auto stats = client.GetTokenUsageStats();
  ASSERT_TRUE(stats.has_value());
  EXPECT_EQ(stats->total_tokens_used, 110);
  EXPECT_FALSE(client.IsNearContextLimit(80.0));

  // Aggregated (billing) usage includes the temp request
  auto aggregated = client.GetAggregatedUsage();
  EXPECT_EQ(aggregated.input_tokens, 1000);
  EXPECT_EQ(aggregated.output_tokens, 60);
}

// Context limit is based on the last main-history request, not the
// cumulative usage.
TEST(OpenAIClientTest, IsNearContextLimitUsesLastRequest) {
  OpenAIEndpoint endpoint;
  endpoint.context_size_ = 1000;
  OpenAIClient client(endpoint);
  EXPECT_FALSE(client.IsNearContextLimit(80.0));

  Usage usage;
  usage.input_tokens = 300;
  usage.output_tokens = 0;
  // Cumulative usage (900) would exceed 80%, the last request (300) does not.
  client.SetLastRequestUsage(usage);
  client.SetLastRequestUsage(usage);
  client.SetLastRequestUsage(usage);
  EXPECT_FALSE(client.IsNearContextLimit(80.0));

  usage.input_tokens = 900;
  client.SetLastRequestUsage(usage);
  EXPECT_TRUE(client.IsNearContextLimit(80.0));
}

// Test transport type configuration
TEST(OpenAIClientTest, TransportType) {
  OpenAIClient client;

  client.SetTransportType(TransportType::httplib);
  EXPECT_EQ(client.GetTransportType(), TransportType::httplib);

  client.SetTransportType(TransportType::curl);
  EXPECT_EQ(client.GetTransportType(), TransportType::curl);
}

// Test SSL verification configuration
TEST(OpenAIClientTest, SSLVerification) {
  OpenAIEndpoint endpoint;
  endpoint.verify_server_ssl_ = true;

  OpenAIClient client(endpoint);
  // The client should be created successfully with SSL verification enabled
  EXPECT_EQ(client.GetEndpointKind(), EndpointKind::openai);

  endpoint.verify_server_ssl_ = false;
  OpenAIClient client2(endpoint);
  // The client should also work with SSL verification disabled
  EXPECT_EQ(client2.GetEndpointKind(), EndpointKind::openai);
}

// Test multiple models configuration
TEST(OpenAIClientTest, MultipleModels) {
  OpenAIEndpoint endpoint;
  endpoint.models_ = {"gpt-4", "gpt-3.5-turbo", "gpt-4-turbo"};
  endpoint.model_ = "gpt-4";

  OpenAIClient client(endpoint);
  EXPECT_EQ(client.GetModel(), "gpt-4");
}

// Test that OpenAI client properly inherits from OllamaClient
TEST(OpenAIClientTest, InheritanceCheck) {
  OpenAIEndpoint endpoint;
  OpenAIClient client(endpoint);

  // Should be able to call base class methods
  client.Startup();
  client.Shutdown();

  // Check interrupt functionality
  client.Interrupt();
  EXPECT_TRUE(client.IsInterrupted());

  client.Startup();
  EXPECT_FALSE(client.IsInterrupted());
}
