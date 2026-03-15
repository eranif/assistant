#include <gtest/gtest.h>

#include "assistant/openai_response_parser.hpp"

using namespace assistant;

// Helper: build a /v1/responses SSE delta line
static std::string DeltaEvent(const std::string& text) {
  return "event: response.output_text.delta\ndata: {\"delta\":\"" + text +
         "\"}\n";
}

static std::string CompletedEvent(int input_tokens, int output_tokens) {
  return "event: response.completed\ndata: {\"status\":\"completed\","
         "\"usage\":{\"input_tokens\":" +
         std::to_string(input_tokens) + ",\"output_tokens\":" +
         std::to_string(output_tokens) + "}}\n";
}

TEST(OpenAIResponseParserTest, BasicStreamingResponse) {
  OpenAIResponseParser parser;
  std::string message = DeltaEvent("Hello");

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_EQ(tokens[0].content, "Hello");
  EXPECT_FALSE(tokens[0].is_done);
  EXPECT_FALSE(tokens[0].HasError());
}

TEST(OpenAIResponseParserTest, StreamCompletedEvent) {
  OpenAIResponseParser parser;
  std::string message = DeltaEvent("Hello") + CompletedEvent(10, 5);

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_GE(tokens.size(), 2);
  EXPECT_EQ(tokens[0].content, "Hello");
  EXPECT_FALSE(tokens[0].is_done);
  EXPECT_TRUE(tokens.back().is_done);
}

TEST(OpenAIResponseParserTest, MultipleChunks) {
  OpenAIResponseParser parser;
  std::string message =
      DeltaEvent("Hello") + DeltaEvent(" World") + DeltaEvent("!");

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 3);
  EXPECT_EQ(tokens[0].content, "Hello");
  EXPECT_EQ(tokens[1].content, " World");
  EXPECT_EQ(tokens[2].content, "!");
}

TEST(OpenAIResponseParserTest, FinishReasonFromStatus) {
  OpenAIResponseParser parser;
  std::string message = CompletedEvent(5, 3);

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens[0].finish_reason.has_value());
  EXPECT_EQ(tokens[0].finish_reason.value(), "completed");
  EXPECT_TRUE(tokens[0].is_done);
}

TEST(OpenAIResponseParserTest, UsageInformation) {
  OpenAIResponseParser parser;
  std::string message = CompletedEvent(10, 20);

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens[0].usage.has_value());
  EXPECT_EQ(tokens[0].usage->input_tokens, 10);
  EXPECT_EQ(tokens[0].usage->output_tokens, 20);
}

TEST(OpenAIResponseParserTest, ErrorResponse) {
  OpenAIResponseParser parser;
  std::string message =
      "event: response.failed\n"
      "data: {\"error\":{\"message\":\"Rate limit exceeded\","
      "\"type\":\"rate_limit_error\"}}\n";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens[0].HasError());
  EXPECT_EQ(tokens[0].error_message.value(), "Rate limit exceeded");
  EXPECT_TRUE(tokens[0].is_done);
}

TEST(OpenAIResponseParserTest, PartialLineBuffering) {
  OpenAIResponseParser parser;

  std::string part1 = "event: response.output_text.delta\ndata: {\"delta\":\"Hel";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(part1, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  EXPECT_EQ(tokens.size(), 0);

  std::string part2 = "lo\"}\n";
  parser.Parse(part2, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_EQ(tokens[0].content, "Hello");
}

TEST(OpenAIResponseParserTest, MalformedJSON) {
  OpenAIResponseParser parser;
  std::string message = "event: response.output_text.delta\ndata: {invalid}\n";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens[0].HasError());
  EXPECT_TRUE(tokens[0].is_done);
}

TEST(OpenAIResponseParserTest, EmptyLinesSkipped) {
  OpenAIResponseParser parser;
  std::string message =
      "\n"
      "event: response.output_text.delta\n"
      "data: {\"delta\":\"Hello\"}\n"
      "\n" +
      CompletedEvent(5, 2);

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_GE(tokens.size(), 2);
  EXPECT_EQ(tokens[0].content, "Hello");
  EXPECT_TRUE(tokens.back().is_done);
}

TEST(OpenAIResponseParserTest, NonDataLinesIgnored) {
  OpenAIResponseParser parser;
  std::string message =
      "id: 123\n"
      "event: response.output_text.delta\n"
      "data: {\"delta\":\"Hello\"}\n";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_EQ(tokens[0].content, "Hello");
}

TEST(OpenAIResponseParserTest, CompleteStreamingSession) {
  OpenAIResponseParser parser;
  std::string message = DeltaEvent("The") + DeltaEvent(" weather") +
                        DeltaEvent(" is") + DeltaEvent(" nice") +
                        CompletedEvent(15, 4);

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  std::string full_content;
  for (const auto& token : tokens) {
    full_content += token.content;
  }
  EXPECT_EQ(full_content, "The weather is nice");

  bool found_usage = false;
  for (const auto& token : tokens) {
    if (token.usage.has_value()) {
      EXPECT_EQ(token.usage->input_tokens, 15);
      EXPECT_EQ(token.usage->output_tokens, 4);
      found_usage = true;
    }
  }
  EXPECT_TRUE(found_usage);
  EXPECT_TRUE(tokens.back().is_done);
}

TEST(OpenAIResponseParserTest, CompletedResponseWithOutputArray) {
  OpenAIResponseParser parser;
  std::string message =
      "event: response.completed\n"
      "data: {\"status\":\"completed\","
      "\"output\":[{\"content\":[{\"text\":\"Hello, how can I help?\"}]}],"
      "\"usage\":{\"input_tokens\":10,\"output_tokens\":6}}\n";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_EQ(tokens[0].content, "Hello, how can I help?");
  EXPECT_TRUE(tokens[0].finish_reason.has_value());
  EXPECT_EQ(tokens[0].finish_reason.value(), "completed");
  EXPECT_TRUE(tokens[0].usage.has_value());
  EXPECT_EQ(tokens[0].usage->input_tokens, 10);
  EXPECT_EQ(tokens[0].usage->output_tokens, 6);
}

TEST(OpenAIResponseParserTest, CarriageReturnHandling) {
  OpenAIResponseParser parser;
  std::string message =
      "event: response.output_text.delta\r\n"
      "data: {\"delta\":\"Hello\"}\r\n";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_EQ(tokens[0].content, "Hello");
}
