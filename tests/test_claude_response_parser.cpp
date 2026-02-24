#include <gtest/gtest.h>

#include "assistant/claude_response_parser.hpp"

namespace assistant::claude {

TEST(ResponseParserTest, ParseTextContent) {
  ResponseParser parser;
  ParseResult result;

  std::string message = R"(
event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}
event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello World"}}
)";

  std::vector<ParseResult> tokens;
  parser.Parse(message, [&tokens](ParseResult result) {
    tokens.push_back(std::move(result));
  });

  EXPECT_EQ(tokens.size(), 2);
  EXPECT_FALSE(tokens[0].is_done);
  EXPECT_FALSE(tokens[0].need_more_data);
  EXPECT_TRUE(tokens[0].content_type.has_value());
  EXPECT_EQ(tokens[0].content_type.value(), ContentType::text);
  EXPECT_EQ(tokens[0].content, "Hello World");

  EXPECT_TRUE(tokens.back().need_more_data);
  EXPECT_FALSE(tokens.back().is_done);
}

TEST(ResponseParserTest, ParseToolUseContent) {
  ResponseParser parser;
  std::string message = R"(
event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"tool_use","name":"calculator","id":"toolu_1234567890"}}
event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"input_json_delta","partial_json":"{\"a\": 5, \"b\": 3"}}
event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"input_json_delta","partial_json":"}"}}
event: content_block_stop
data: {"type":"content_block_stop","index":0}
)";

  std::vector<ParseResult> tokens;
  parser.Parse(message, [&tokens](ParseResult result) {
    tokens.push_back(std::move(result));
  });
  EXPECT_EQ(tokens.size(), 2);
  EXPECT_FALSE(tokens[0].is_done);
  EXPECT_FALSE(tokens[0].need_more_data);
  EXPECT_TRUE(tokens[0].content_type.has_value());
  EXPECT_EQ(tokens[0].content_type.value(), ContentType::tool_use);
  EXPECT_FALSE(tokens[0].GetToolJsonStr().empty());
  EXPECT_NO_THROW(auto res = json::parse(tokens[0].GetToolJsonStr()))
      << "Failed to parse: " << tokens[0].content;

  EXPECT_TRUE(tokens.back().need_more_data);
  EXPECT_FALSE(tokens.back().is_done);
}

TEST(ResponseParserTest, ParseThinkingContent) {
  ResponseParser parser;
  std::string message = R"(
event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"thinking","text":""}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"\n1. First step"}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"\n2. Second step"}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}
)";

  std::vector<ParseResult> tokens;
  parser.Parse(message, [&tokens](ParseResult result) {
    tokens.push_back(std::move(result));
  });

  EXPECT_EQ(tokens.size(), 3);
  EXPECT_FALSE(tokens[0].IsDone());
  EXPECT_FALSE(tokens[0].NeedMoreData());
  EXPECT_TRUE(tokens[0].content_type.has_value());
  EXPECT_EQ(tokens[0].content_type.value(), ContentType::thinking);
  EXPECT_EQ(tokens[0].content, "\n1. First step");

  EXPECT_FALSE(tokens[1].IsDone());
  EXPECT_FALSE(tokens[1].NeedMoreData());
  EXPECT_TRUE(tokens[1].content_type.has_value());
  EXPECT_EQ(tokens[1].content_type.value(), ContentType::thinking);
  EXPECT_EQ(tokens[1].content, "\n2. Second step");

  EXPECT_TRUE(tokens.back().need_more_data);
  EXPECT_FALSE(tokens.back().is_done);
}

TEST(ResponseParserTest, ParseCompleteMessage) {
  ResponseParser parser;
  std::string message = R"(
event: message_start
data: {}

event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello World"}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: message_stop
data: {"type":"message_stop","index":0}
)";

  std::vector<ParseResult> tokens;
  parser.Parse(message, [&tokens](ParseResult result) {
    tokens.push_back(std::move(result));
  });

  EXPECT_EQ(tokens.size(), 2);

  EXPECT_FALSE(tokens[0].is_done);
  EXPECT_FALSE(tokens[0].need_more_data);
  EXPECT_TRUE(tokens[0].content_type.has_value());
  EXPECT_EQ(tokens[0].content_type.value(), ContentType::text);
  EXPECT_EQ(tokens[0].content, "Hello World");

  EXPECT_FALSE(tokens.back().need_more_data);
  EXPECT_TRUE(tokens.back().is_done);
}

TEST(ResponseParserTest, ParseWithPartialData) {
  ResponseParser parser;
  std::string message =
      R"(
event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}
)";

  // First parse with partial data
  std::vector<ParseResult> tokens;
  parser.Parse(message, [&tokens](ParseResult result) {
    tokens.push_back(std::move(result));
  });
  EXPECT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens.back().need_more_data);
  EXPECT_FALSE(tokens.back().is_done);

  // Add more data and parse again
  std::string message2 = R"(
event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello World"}}
)";

  tokens.clear();
  parser.Parse(message2, [&tokens](ParseResult result) {
    tokens.push_back(std::move(result));
  });
  EXPECT_EQ(tokens.size(), 2);

  EXPECT_TRUE(tokens[0].HasValue());
  EXPECT_FALSE(tokens[0].IsDone());
  EXPECT_TRUE(tokens[0].content_type.has_value());
  EXPECT_EQ(tokens[0].content_type.value(), ContentType::text);
  EXPECT_EQ(tokens[0].content, "Hello World");

  EXPECT_TRUE(tokens.back().NeedMoreData());
  EXPECT_FALSE(tokens.back().IsDone());
}

TEST(ResponseParserTest, ErrorMessage) {
  ResponseParser parser;
  std::string message = R"(
event: error
data: {"type":"error","error":{"type":"invalid_request_error","message":"messages.1.content: Input should be a valid list"},"request_id":"req_011CTtPr3mnnjHJoWCFAK77W"}
)";

  std::vector<ParseResult> tokens;
  parser.Parse(message, [&tokens](ParseResult result) {
    tokens.push_back(std::move(result));
  });
  
  EXPECT_EQ(tokens.size(), 1);
  EXPECT_TRUE(tokens[0].is_done);
  EXPECT_TRUE(tokens[0].stop_reason.has_value());
  EXPECT_EQ(tokens[0].stop_reason.value(), StopReason::error);
}
}  // namespace assistant::claude
