#include <gtest/gtest.h>

#include "assistant/openai_response_parser.hpp"

using namespace assistant;

// Test basic OpenAI streaming response parsing
TEST(OpenAIResponseParserTest, BasicStreamingResponse) {
  OpenAIResponseParser parser;
  std::string message =
      R"(data: {"id":"chatcmpl-123","choices":[{"delta":{"content":"Hello"},"index":0}]}
)";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_EQ(tokens[0].content, "Hello");
  EXPECT_FALSE(tokens[0].is_done);
  EXPECT_FALSE(tokens[0].HasError());
}

// Test OpenAI stream completion with [DONE] marker
TEST(OpenAIResponseParserTest, StreamDoneMarker) {
  OpenAIResponseParser parser;
  std::string message =
      R"(data: {"id":"chatcmpl-123","choices":[{"delta":{"content":"Hello"},"index":0}]}
data: [DONE]
)";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 2);
  EXPECT_EQ(tokens[0].content, "Hello");
  EXPECT_FALSE(tokens[0].is_done);
  EXPECT_TRUE(tokens[1].is_done);
  EXPECT_TRUE(tokens[1].content.empty());
}

// Test multiple chunks in one parse call
TEST(OpenAIResponseParserTest, MultipleChunks) {
  OpenAIResponseParser parser;
  std::string message =
      R"(data: {"id":"chatcmpl-123","choices":[{"delta":{"content":"Hello"},"index":0}]}
data: {"id":"chatcmpl-123","choices":[{"delta":{"content":" World"},"index":0}]}
data: {"id":"chatcmpl-123","choices":[{"delta":{"content":"!"},"index":0}]}
)";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 3);
  EXPECT_EQ(tokens[0].content, "Hello");
  EXPECT_EQ(tokens[1].content, " World");
  EXPECT_EQ(tokens[2].content, "!");
}

// Test finish reason parsing
TEST(OpenAIResponseParserTest, FinishReason) {
  OpenAIResponseParser parser;
  std::string message =
      R"(data: {"id":"chatcmpl-123","choices":[{"delta":{},"finish_reason":"stop","index":0}]}
)";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens[0].finish_reason.has_value());
  EXPECT_EQ(tokens[0].finish_reason.value(), "stop");
  EXPECT_TRUE(tokens[0].is_done);
}

// Test usage information parsing
TEST(OpenAIResponseParserTest, UsageInformation) {
  OpenAIResponseParser parser;
  std::string message =
      R"(data: {"id":"chatcmpl-123","choices":[{"delta":{},"index":0}],"usage":{"prompt_tokens":10,"completion_tokens":20,"total_tokens":30}}
)";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens[0].usage.has_value());
  EXPECT_EQ(tokens[0].usage->input_tokens, 10);
  EXPECT_EQ(tokens[0].usage->output_tokens, 20);
}

// Test error handling
TEST(OpenAIResponseParserTest, ErrorResponse) {
  OpenAIResponseParser parser;
  std::string message =
      R"(data: {"error":{"message":"Rate limit exceeded","type":"rate_limit_error"}}
)";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens[0].HasError());
  EXPECT_EQ(tokens[0].error_message.value(), "Rate limit exceeded");
  EXPECT_TRUE(tokens[0].is_done);
}

// Test empty delta (role assignment)
TEST(OpenAIResponseParserTest, EmptyDelta) {
  OpenAIResponseParser parser;
  std::string message =
      R"(data: {"id":"chatcmpl-123","choices":[{"delta":{"role":"assistant"},"index":0}]}
)";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  // Empty delta (no content) should not produce a token
  EXPECT_EQ(tokens.size(), 0);
}

// Test partial line buffering
TEST(OpenAIResponseParserTest, PartialLineBuffering) {
  OpenAIResponseParser parser;

  // First part without newline
  std::string part1 =
      R"(data: {"id":"chatcmpl-123","choices":[{"delta":{"content":"Hel)";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(part1, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  // Should not parse anything yet
  EXPECT_EQ(tokens.size(), 0);

  // Second part completes the line
  std::string part2 = R"(lo"},"index":0}]}
)";

  parser.Parse(part2, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  // Now should have parsed the complete message
  ASSERT_EQ(tokens.size(), 1);
  EXPECT_EQ(tokens[0].content, "Hello");
}

// Test handling of malformed JSON
TEST(OpenAIResponseParserTest, MalformedJSON) {
  OpenAIResponseParser parser;
  std::string message = R"(data: {invalid json}
)";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens[0].HasError());
  EXPECT_TRUE(tokens[0].is_done);
}

// Test empty lines are skipped
TEST(OpenAIResponseParserTest, EmptyLinesSkipped) {
  OpenAIResponseParser parser;
  std::string message = R"(
data: {"id":"chatcmpl-123","choices":[{"delta":{"content":"Hello"},"index":0}]}

data: [DONE]
)";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 2);
  EXPECT_EQ(tokens[0].content, "Hello");
  EXPECT_TRUE(tokens[1].is_done);
}

// Test non-data lines are ignored
TEST(OpenAIResponseParserTest, NonDataLinesIgnored) {
  OpenAIResponseParser parser;
  std::string message = R"(event: message
data: {"id":"chatcmpl-123","choices":[{"delta":{"content":"Hello"},"index":0}]}
id: 123
)";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_EQ(tokens[0].content, "Hello");
}

// Test complete streaming session
TEST(OpenAIResponseParserTest, CompleteStreamingSession) {
  OpenAIResponseParser parser;
  std::string message =
      R"(data: {"id":"chatcmpl-123","object":"chat.completion.chunk","created":1234567890,"model":"gpt-4","choices":[{"delta":{"role":"assistant"},"index":0}]}
data: {"id":"chatcmpl-123","object":"chat.completion.chunk","created":1234567890,"model":"gpt-4","choices":[{"delta":{"content":"The"},"index":0}]}
data: {"id":"chatcmpl-123","object":"chat.completion.chunk","created":1234567890,"model":"gpt-4","choices":[{"delta":{"content":" weather"},"index":0}]}
data: {"id":"chatcmpl-123","object":"chat.completion.chunk","created":1234567890,"model":"gpt-4","choices":[{"delta":{"content":" is"},"index":0}]}
data: {"id":"chatcmpl-123","object":"chat.completion.chunk","created":1234567890,"model":"gpt-4","choices":[{"delta":{"content":" nice"},"index":0}]}
data: {"id":"chatcmpl-123","object":"chat.completion.chunk","created":1234567890,"model":"gpt-4","choices":[{"delta":{},"finish_reason":"stop","index":0}],"usage":{"prompt_tokens":15,"completion_tokens":4,"total_tokens":19}}
data: [DONE]
)";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  // Should get: The, weather, is, nice, finish_reason+usage, [DONE]
  EXPECT_GE(tokens.size(), 5);

  // Verify content tokens
  std::string full_content;
  for (const auto& token : tokens) {
    if (!token.content.empty()) {
      full_content += token.content;
    }
  }
  EXPECT_EQ(full_content, "The weather is nice");

  // Verify last meaningful token has usage
  bool found_usage = false;
  for (const auto& token : tokens) {
    if (token.usage.has_value()) {
      EXPECT_EQ(token.usage->input_tokens, 15);
      EXPECT_EQ(token.usage->output_tokens, 4);
      found_usage = true;
    }
  }
  EXPECT_TRUE(found_usage);

  // Verify stream ended properly
  EXPECT_TRUE(tokens.back().is_done);
}

// Test non-streaming response format
TEST(OpenAIResponseParserTest, NonStreamingResponse) {
  OpenAIResponseParser parser;
  std::string message =
      R"(data: {"id":"chatcmpl-123","object":"chat.completion","created":1234567890,"model":"gpt-4","choices":[{"message":{"role":"assistant","content":"Hello, how can I help?"},"finish_reason":"stop","index":0}],"usage":{"prompt_tokens":10,"completion_tokens":6,"total_tokens":16}}
)";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_EQ(tokens[0].content, "Hello, how can I help?");
  EXPECT_TRUE(tokens[0].finish_reason.has_value());
  EXPECT_EQ(tokens[0].finish_reason.value(), "stop");
  EXPECT_TRUE(tokens[0].usage.has_value());
  EXPECT_EQ(tokens[0].usage->input_tokens, 10);
  EXPECT_EQ(tokens[0].usage->output_tokens, 6);
}

// Test carriage return handling
TEST(OpenAIResponseParserTest, CarriageReturnHandling) {
  OpenAIResponseParser parser;
  std::string message =
      "data: {\"id\":\"chatcmpl-123\",\"choices\":[{\"delta\":{\"content\":"
      "\"Hello\"},\"index\":0}]}\r\n";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_EQ(tokens[0].content, "Hello");
}
