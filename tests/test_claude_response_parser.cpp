#include <gtest/gtest.h>

#include "assistant/claude_response_parser.hpp"

namespace assistant::claude {

TEST(ResponseParserTest, ParseTextContent) {
  ResponseParser parser;
  std::string message =
      "event: content_block_start\n"
      "data: "
      "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{"
      "\"type\":\"text\",\"text\":\"\"}}\n"
      "event: content_block_delta\n"
      "data: "
      "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":"
      "\"text_delta\",\"text\":\"Hello World\"}}\n";

  auto result = parser.Parse(message);
  EXPECT_FALSE(result.is_done);
  EXPECT_FALSE(result.need_more_data);
  EXPECT_TRUE(result.content_type.has_value());
  EXPECT_EQ(result.content_type.value(), ContentType::text);
  EXPECT_EQ(result.text, "Hello World");
}

TEST(ResponseParserTest, ParseToolUseContent) {
  ResponseParser parser;
  std::string message =
      "event: content_block_start\n"
      "data: "
      "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{"
      "\"type\":\"tool_use\",\"name\":\"calculator\",\"id\":\"toolu_"
      "1234567890\"}}\n"
      "event: content_block_delta\n"
      "data: "
      "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":"
      "\"input_json_delta\",\"partial_json\":\"{\\\"a\\\": 5, \\\"b\\\": "
      "3\"}}\n"
      "event: content_block_delta\n"
      "data: "
      "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":"
      "\"input_json_delta\",\"partial_json\":\"}\"}}\n"
      "event: content_block_stop\n"
      "data: {\"type\":\"content_block_stop\",\"index\":0}\n";

  auto result = parser.Parse(message);
  EXPECT_FALSE(result.is_done);
  EXPECT_FALSE(result.need_more_data);
  EXPECT_TRUE(result.content_type.has_value());
  EXPECT_EQ(result.content_type.value(), ContentType::tool_use);
  // The tool use JSON should be parsed into a valid JSON object
  EXPECT_FALSE(result.text.empty());
}

TEST(ResponseParserTest, ParseThinkingContent) {
  ResponseParser parser;
  std::string message =
      "event: content_block_start\n"
      "data: "
      "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{"
      "\"type\":\"thinking\",\"text\":\"\"}}\n"
      "event: content_block_delta\n"
      "data: "
      "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":"
      "\"thinking_delta\",\"thinking\":\"\\n1. First step\"}}\n"
      "event: content_block_delta\n"
      "data: "
      "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":"
      "\"thinking_delta\",\"thinking\":\"\\n2. Second step\"}}\n"
      "event: content_block_stop\n"
      "data: {\"type\":\"content_block_stop\",\"index\":0}\n";

  auto result = parser.Parse(message);
  EXPECT_FALSE(result.is_done);
  EXPECT_FALSE(result.need_more_data);
  EXPECT_TRUE(result.content_type.has_value());
  EXPECT_EQ(result.content_type.value(), ContentType::thinking);
  EXPECT_FALSE(result.text.empty());
}

TEST(ResponseParserTest, ParseCompleteMessage) {
  ResponseParser parser;
  std::string message =R"(
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

  auto result = parser.Parse(message);
  EXPECT_TRUE(result.is_done);
  EXPECT_FALSE(result.need_more_data);
  EXPECT_TRUE(result.content_type.has_value());
  EXPECT_EQ(result.content_type.value(), ContentType::text);
  EXPECT_EQ(result.text, "Hello World");
}

TEST(ResponseParserTest, ParseWithPartialData) {
  ResponseParser parser;
  std::string message =
      "event: content_block_start\n"
      "data: "
      "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{"
      "\"type\":\"text\",\"text\":\"\"}}\n";

  // First parse with partial data
  auto result1 = parser.Parse(message);
  EXPECT_TRUE(result1.need_more_data);
  EXPECT_FALSE(result1.is_done);

  // Add more data and parse again
  std::string message2 =
      "event: content_block_delta\n"
      "data: "
      "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":"
      "\"text_delta\",\"text\":\"Hello World\"}}\n";

  auto result2 = parser.Parse(message2);
  EXPECT_FALSE(result2.need_more_data);
  EXPECT_FALSE(result2.is_done);
  EXPECT_TRUE(result2.content_type.has_value());
  EXPECT_EQ(result2.content_type.value(), ContentType::text);
  EXPECT_EQ(result2.text, "Hello World");
}

}  // namespace assistant::claude