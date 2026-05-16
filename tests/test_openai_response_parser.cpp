#include <gtest/gtest.h>

#include "assistant/openai_response_parser.hpp"

using namespace assistant;

// Helper: build a /v1/responses SSE delta line
static std::string DeltaEvent(const std::string& text) {
  return "event: response.output_text.delta\ndata: "
         "{\"type\":\"response.output_text.delta\",\"delta\":\"" +
         text + "\"}\n";
}

static std::string CompletedEvent(int input_tokens, int output_tokens) {
  return "event: response.completed\ndata: {\"type\":\"response.completed\","
         "\"status\":\"completed\","
         "\"response\":{\"usage\":{\"input_tokens\":" +
         std::to_string(input_tokens) +
         ",\"output_tokens\":" + std::to_string(output_tokens) + "}}}\n";
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
  EXPECT_FALSE(tokens[0].IsError());
}

TEST(OpenAIResponseParserTest, StreamCompletedEvent) {
  OpenAIResponseParser parser;
  std::string message = DeltaEvent("Hello") + CompletedEvent(10, 5);

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  std::string content;
  parser.Parse(message,
               [&tokens, &content](OpenAIResponseParser::ParseResult result) {
                 content += result.content;
                 tokens.push_back(std::move(result));
               });

  ASSERT_GE(tokens.size(), 2);
  EXPECT_EQ(content, "Hello");
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
      "event: error\n"
      "data: {\"type\":\"error\",\"error\":{\"message\":\"Rate limit "
      "exceeded\","
      "\"type\":\"rate_limit_error\"}}\n";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens[0].IsError());
  EXPECT_EQ(tokens[0].GetErrorMessage(), "Rate limit exceeded");
  EXPECT_TRUE(tokens[0].is_done);
}

TEST(OpenAIResponseParserTest, ErrorResponseInReponseFailed) {
  OpenAIResponseParser parser;
  std::string event = "event: response.failed";
  std::string payload =
      R"#(data: {
  "type": "response.failed",
  "response": {
    "id": "resp_04f9868dfa36013f006a0848139b2c8195b8cf8e1129416dad",
    "object": "response",
    "created_at": 1778927635,
    "status": "failed",
    "background": false,
    "completed_at": null,
    "error": {
      "code": "rate_limit_exceeded",
      "message": "Rate limit reached for gpt-5.4-mini in organization org-3gD10Y83VnT5wNvdBtn8ZcGD on tokens per min (TPM): Limit 200000, Used 169705, Requested 36831. Please try again in 1.96s. Visit https://platform.openai.com/account/rate-limits to learn more."
    },
    "frequency_penalty": 0,
    "incomplete_details": null,
    "instructions": null,
    "max_output_tokens": 64000,
    "max_tool_calls": null,
    "model": "gpt-5.4-mini-2026-03-17",
    "moderation": null,
    "output": [],
    "parallel_tool_calls": true,
    "presence_penalty": 0,
    "previous_response_id": null,
    "prompt_cache_key": null,
    "prompt_cache_retention": "in_memory",
    "reasoning": {
      "effort": "none",
      "summary": null
    },
    "safety_identifier": null,
    "service_tier": "auto",
    "store": true,
    "temperature": 1,
    "text": {
      "format": {
        "type": "text"
      },
      "verbosity": "medium"
    },
    "tool_choice": "auto",
    "top_logprobs": 0,
    "top_p": 0.98,
    "truncation": "disabled",
    "usage": null,
    "user": null,
    "metadata": {}
  },
  "sequence_number": 3
})#";

  payload.erase(std::remove(payload.begin(), payload.end(), '\n'),
                payload.end());
  payload.erase(std::remove(payload.begin(), payload.end(), '\r'),
                payload.end());
  std::string message = event + "\n" + payload + "\n";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens[0].IsError());
  EXPECT_TRUE(tokens[0].GetErrorMessage().starts_with(
      "Rate limit reached for gpt-5.4-mini"));
  EXPECT_TRUE(tokens[0].is_done);
}

TEST(OpenAIResponseParserTest, PartialLineBuffering) {
  OpenAIResponseParser parser;

  std::string part1 =
      "event: response.output_text.delta\ndata: "
      "{\"type\":\"response.output_text.delta\",\"delta\":\"Hel";

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
  std::string message =
      "event: response.output_text.delta\ndata: "
      "{\"type\":\"response.output_text.delta\",invalid}\n";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens[0].IsError());
  EXPECT_TRUE(tokens[0].is_done);
}

TEST(OpenAIResponseParserTest, EmptyLinesSkipped) {
  OpenAIResponseParser parser;
  std::string message =
      "\n"
      "event: response.output_text.delta\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"Hello\"}\n"
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
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"Hello\"}\n";

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

TEST(OpenAIResponseParserTest, CarriageReturnHandling) {
  OpenAIResponseParser parser;
  std::string message =
      "event: response.output_text.delta\r\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"Hello\"}\r\n";

  std::vector<OpenAIResponseParser::ParseResult> tokens;
  parser.Parse(message, [&tokens](OpenAIResponseParser::ParseResult result) {
    tokens.push_back(std::move(result));
  });

  ASSERT_EQ(tokens.size(), 1);
  EXPECT_EQ(tokens[0].content, "Hello");
}
