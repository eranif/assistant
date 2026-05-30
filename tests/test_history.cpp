#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "assistant/client/claude_client.hpp"
#include "assistant/client/client_base.hpp"
#include "assistant/client/ollama_client.hpp"
#include "assistant/client/openai_client.hpp"
#include "assistant/client/openai_messages_client.hpp"

using namespace assistant;

// Test fixture for History tests
class HistoryTest : public ::testing::Test {
 protected:
  void SetUp() override { history_ = std::make_unique<History>(); }

  void TearDown() override { history_.reset(); }

  std::unique_ptr<History> history_;
};

// Test: Initial state
TEST_F(HistoryTest, InitialState) {
  EXPECT_TRUE(history_->IsEmpty());
  EXPECT_EQ(history_->GetSwapCount(), 0);
  EXPECT_FALSE(history_->IsTempHistory());
  EXPECT_EQ(history_->GetMessages().size(), 0);
}

// Test: Add single message
TEST_F(HistoryTest, AddSingleMessage) {
  assistant::message msg{"user", "Hello, world!"};
  history_->AddMessage(msg);

  EXPECT_FALSE(history_->IsEmpty());
  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0]["role"].get<std::string>(), "user");
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Hello, world!");
}

// Test: Add multiple messages
TEST_F(HistoryTest, AddMultipleMessages) {
  history_->AddMessage(assistant::message{"user", "First message"});
  history_->AddMessage(assistant::message{"assistant", "Second message"});
  history_->AddMessage(assistant::message{"user", "Third message"});

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 3);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "First message");
  EXPECT_EQ(messages[1]["content"].get<std::string>(), "Second message");
  EXPECT_EQ(messages[2]["content"].get<std::string>(), "Third message");
}

// Test: Add optional message with value
TEST_F(HistoryTest, AddOptionalMessageWithValue) {
  std::optional<assistant::message> msg = assistant::message{"user", "Test"};
  history_->AddMessage(msg, MessageType::kNormal);

  EXPECT_FALSE(history_->IsEmpty());
  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Test");
}

// Test: Add optional message without value
TEST_F(HistoryTest, AddOptionalMessageWithoutValue) {
  std::optional<assistant::message> msg = std::nullopt;
  history_->AddMessage(msg, MessageType::kNormal);

  EXPECT_TRUE(history_->IsEmpty());
  EXPECT_EQ(history_->GetMessages().size(), 0);
}

// Test: Clear history
TEST_F(HistoryTest, ClearHistory) {
  history_->AddMessage(assistant::message{"user", "Message 1"});
  history_->AddMessage(assistant::message{"user", "Message 2"});
  EXPECT_FALSE(history_->IsEmpty());

  history_->Clear();

  EXPECT_TRUE(history_->IsEmpty());
  EXPECT_EQ(history_->GetMessages().size(), 0);
}

// Test: ClearAll clears both main and temp history
TEST_F(HistoryTest, ClearAll) {
  // Add to main history
  history_->AddMessage(assistant::message{"user", "Main message"});

  // Switch to temp and add message
  history_->SwapToTempHistory();
  history_->AddMessage(assistant::message{"user", "Temp message"});

  // Clear all
  history_->ClearAll();

  // Check temp history is empty
  EXPECT_TRUE(history_->IsEmpty());

  // Switch back to main and check it's also empty
  history_->SwapToMainHistory();
  EXPECT_TRUE(history_->IsEmpty());
}

// Test: SetMessages
TEST_F(HistoryTest, SetMessages) {
  assistant::messages new_messages;
  new_messages.push_back(assistant::message{"user", "New message 1"});
  new_messages.push_back(assistant::message{"assistant", "New message 2"});

  history_->SetMessages(new_messages);

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 2);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "New message 1");
  EXPECT_EQ(messages[1]["content"].get<std::string>(), "New message 2");
}

// Test: SetMessages replaces existing messages
TEST_F(HistoryTest, SetMessagesReplacesExisting) {
  history_->AddMessage(assistant::message{"user", "Old message"});

  assistant::messages new_messages;
  new_messages.push_back(assistant::message{"user", "Replacement message"});
  history_->SetMessages(new_messages);

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Replacement message");
}
// Test: Swap to temp history once
TEST_F(HistoryTest, SwapToTempHistoryOnce) {
  history_->AddMessage(assistant::message{"user", "Main message"});

  history_->SwapToTempHistory();

  EXPECT_TRUE(history_->IsTempHistory());
  EXPECT_EQ(history_->GetSwapCount(), 1);
  EXPECT_TRUE(history_->IsEmpty());  // Temp history is empty
}

// Test: Swap to temp history multiple times
TEST_F(HistoryTest, SwapToTempHistoryMultipleTimes) {
  history_->SwapToTempHistory();
  EXPECT_EQ(history_->GetSwapCount(), 1);
  EXPECT_TRUE(history_->IsTempHistory());

  history_->SwapToTempHistory();
  EXPECT_EQ(history_->GetSwapCount(), 2);
  EXPECT_TRUE(history_->IsTempHistory());

  history_->SwapToTempHistory();
  EXPECT_EQ(history_->GetSwapCount(), 3);
  EXPECT_TRUE(history_->IsTempHistory());
}

// Test: Swap back to main history
TEST_F(HistoryTest, SwapToMainHistory) {
  history_->AddMessage(assistant::message{"user", "Main message"});

  history_->SwapToTempHistory();
  EXPECT_TRUE(history_->IsTempHistory());

  history_->SwapToMainHistory();
  EXPECT_FALSE(history_->IsTempHistory());
  EXPECT_EQ(history_->GetSwapCount(), 0);

  // Main message should still be there
  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Main message");
}

// Test: Swap back when already on main history (no-op)
TEST_F(HistoryTest, SwapToMainHistoryWhenAlreadyOnMain) {
  EXPECT_FALSE(history_->IsTempHistory());

  history_->SwapToMainHistory();

  EXPECT_FALSE(history_->IsTempHistory());
  EXPECT_EQ(history_->GetSwapCount(), 0);
}

// Test: Multiple swap operations
TEST_F(HistoryTest, MultipleSwapOperations) {
  history_->AddMessage(assistant::message{"user", "Main 1"});

  history_->SwapToTempHistory();
  history_->AddMessage(assistant::message{"user", "Temp 1"});
  EXPECT_EQ(history_->GetMessages().size(), 1);

  history_->SwapToTempHistory();  // Increase count but still on temp
  EXPECT_EQ(history_->GetSwapCount(), 2);

  history_->SwapToMainHistory();  // Count becomes 1, still on temp
  EXPECT_TRUE(history_->IsTempHistory());
  EXPECT_EQ(history_->GetSwapCount(), 1);

  history_->SwapToMainHistory();  // Count becomes 0, back to main
  EXPECT_FALSE(history_->IsTempHistory());
  EXPECT_EQ(history_->GetSwapCount(), 0);

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Main 1");
}

// Test: Messages in temp history are separate from main
TEST_F(HistoryTest, TempAndMainHistoriesAreSeparate) {
  // Add to main
  history_->AddMessage(assistant::message{"user", "Main message"});
  EXPECT_EQ(history_->GetMessages().size(), 1);

  // Switch to temp
  history_->SwapToTempHistory();
  EXPECT_TRUE(history_->IsEmpty());  // Temp starts empty

  // Add to temp
  history_->AddMessage(assistant::message{"user", "Temp message"});
  EXPECT_EQ(history_->GetMessages().size(), 1);

  // Switch back to main
  history_->SwapToMainHistory();
  auto main_messages = history_->GetMessages();
  ASSERT_EQ(main_messages.size(), 1);
  EXPECT_EQ(main_messages[0]["content"].get<std::string>(), "Main message");

  // Switch back to temp
  history_->SwapToTempHistory();
  auto temp_messages = history_->GetMessages();
  ASSERT_EQ(temp_messages.size(), 1);
  EXPECT_EQ(temp_messages[0]["content"].get<std::string>(), "Temp message");
}

// Test: Thread safety - concurrent AddMessage calls
TEST_F(HistoryTest, ThreadSafetyConcurrentAdds) {
  const int num_threads = 10;
  const int messages_per_thread = 100;
  std::vector<std::thread> threads;

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([this, i, messages_per_thread]() {
      for (int j = 0; j < messages_per_thread; ++j) {
        std::string content =
            "Thread " + std::to_string(i) + " Message " + std::to_string(j);
        history_->AddMessage(assistant::message{"user", content});
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Should have num_threads * messages_per_thread messages
  EXPECT_EQ(history_->GetMessages().size(), num_threads * messages_per_thread);
}

// Test: Thread safety - concurrent swaps and adds
TEST_F(HistoryTest, ThreadSafetyConcurrentSwapsAndAdds) {
  const int num_operations = 1000;
  std::vector<std::thread> threads;

  // Thread 1: Add messages
  threads.emplace_back([this, num_operations]() {
    for (int i = 0; i < num_operations; ++i) {
      history_->AddMessage(
          assistant::message{"user", "Message " + std::to_string(i)});
    }
  });

  // Thread 2: Swap operations
  threads.emplace_back([this, num_operations]() {
    for (int i = 0; i < num_operations; ++i) {
      if (i % 2 == 0) {
        history_->SwapToTempHistory();
      } else {
        history_->SwapToMainHistory();
      }
    }
  });

  // Thread 3: Read operations
  threads.emplace_back([this, num_operations]() {
    for (int i = 0; i < num_operations; ++i) {
      volatile auto size = history_->GetMessages().size();
      volatile bool is_temp = history_->IsTempHistory();
      volatile bool is_empty = history_->IsEmpty();
      (void)size;
      (void)is_temp;
      (void)is_empty;
    }
  });

  for (auto& thread : threads) {
    thread.join();
  }

  // Test should complete without crashes or deadlocks
  SUCCEED();
}

// Test: Thread safety - concurrent Clear and Add
TEST_F(HistoryTest, ThreadSafetyConcurrentClearAndAdd) {
  const int num_operations = 500;
  std::vector<std::thread> threads;

  threads.emplace_back([this, num_operations]() {
    for (int i = 0; i < num_operations; ++i) {
      history_->AddMessage(assistant::message{"user", "Add message"});
    }
  });

  threads.emplace_back([this, num_operations]() {
    for (int i = 0; i < num_operations; ++i) {
      history_->Clear();
    }
  });

  for (auto& thread : threads) {
    thread.join();
  }

  // Test should complete without crashes
  SUCCEED();
}

// Test: Thread safety - concurrent GetMessages
TEST_F(HistoryTest, ThreadSafetyConcurrentReads) {
  history_->AddMessage(assistant::message{"user", "Test message"});

  const int num_threads = 20;
  std::vector<std::thread> threads;

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([this]() {
      for (int j = 0; j < 100; ++j) {
        auto messages = history_->GetMessages();
        // Just access the data
        volatile auto size = messages.size();
        (void)size;
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  SUCCEED();
}

// Test: SetMessages with empty vector
TEST_F(HistoryTest, SetMessagesEmpty) {
  history_->AddMessage(assistant::message{"user", "Message 1"});
  EXPECT_FALSE(history_->IsEmpty());

  assistant::messages empty_messages;
  history_->SetMessages(empty_messages);

  EXPECT_TRUE(history_->IsEmpty());
  EXPECT_EQ(history_->GetMessages().size(), 0);
}

// Test: Complex scenario - mixing operations on both histories
TEST_F(HistoryTest, ComplexMixedOperations) {
  // Add messages to main
  history_->AddMessage(assistant::message{"user", "Main 1"});
  history_->AddMessage(assistant::message{"assistant", "Main 2"});
  EXPECT_EQ(history_->GetMessages().size(), 2);

  // Switch to temp
  history_->SwapToTempHistory();
  history_->AddMessage(assistant::message{"user", "Temp 1"});
  history_->AddMessage(assistant::message{"assistant", "Temp 2"});
  history_->AddMessage(assistant::message{"user", "Temp 3"});
  EXPECT_EQ(history_->GetMessages().size(), 3);

  // Switch back to main
  history_->SwapToMainHistory();
  EXPECT_EQ(history_->GetMessages().size(), 2);

  // Add more to main
  history_->AddMessage(assistant::message{"user", "Main 3"});
  EXPECT_EQ(history_->GetMessages().size(), 3);

  // Clear main
  history_->Clear();
  EXPECT_TRUE(history_->IsEmpty());
}

// Test: Swap count tracking with nested swaps
TEST_F(HistoryTest, SwapCountTracking) {
  EXPECT_EQ(history_->GetSwapCount(), 0);

  history_->SwapToTempHistory();
  EXPECT_EQ(history_->GetSwapCount(), 1);

  history_->SwapToTempHistory();
  EXPECT_EQ(history_->GetSwapCount(), 2);

  history_->SwapToTempHistory();
  EXPECT_EQ(history_->GetSwapCount(), 3);

  history_->SwapToMainHistory();
  EXPECT_EQ(history_->GetSwapCount(), 2);

  history_->SwapToMainHistory();
  EXPECT_EQ(history_->GetSwapCount(), 1);

  history_->SwapToMainHistory();
  EXPECT_EQ(history_->GetSwapCount(), 0);

  // Extra swaps to main should not go negative
  history_->SwapToMainHistory();
  EXPECT_EQ(history_->GetSwapCount(), 0);
}

// Test: Message preservation after multiple swap operations
TEST_F(HistoryTest, MessagePreservationAfterSwaps) {
  // Add initial message
  history_->AddMessage(assistant::message{"user", "Initial"});

  // Swap to temp and back multiple times
  for (int i = 0; i < 5; ++i) {
    history_->SwapToTempHistory();
    history_->SwapToMainHistory();
  }

  // Original message should still be there
  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Initial");
}

// Test: Compact on empty history does nothing
TEST_F(HistoryTest, CompactEmptyHistory) {
  EXPECT_TRUE(history_->IsEmpty());

  bool called = false;
  history_->Compact([&called](assistant::message& /*msg*/) { called = true; });

  EXPECT_FALSE(called);
  EXPECT_TRUE(history_->IsEmpty());
}

// Test: Compact on temp history is a no-op
TEST_F(HistoryTest, CompactOnTempHistory) {
  // Add message to main history
  history_->AddMessage(assistant::message{"user", "Main message"});

  // Swap to temp history
  history_->SwapToTempHistory();
  EXPECT_TRUE(history_->IsTempHistory());

  // Add a message to temp history
  history_->AddMessage(assistant::message{"user", "Temp message"});

  bool called = false;
  history_->Compact([&called](assistant::message& /*msg*/) { called = true; });

  // Callback should not be called when on temp history
  EXPECT_FALSE(called);

  // Temp message should still be there
  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Temp message");
}

// Test: Compact leaves normal messages untouched and preserves the last tool
// response
TEST_F(HistoryTest, CompactTrimsNormalMessages) {
  history_->AddMessage(assistant::message{"user", "Message 1"},
                       MessageType::kNormal);
  history_->AddMessage(assistant::message{"assistant", "Message 2"},
                       MessageType::kNormal);
  history_->AddMessage(assistant::message{"user", "Message 3"},
                       MessageType::kNormal);
  history_->AddMessage(assistant::message{"tool", "Tool response"},
                       MessageType::kToolResponse);

  history_->Compact(
      [](assistant::message& msg) { msg["content"] = "[TRIMMED]"; });

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 4);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Message 1");
  EXPECT_EQ(messages[1]["content"].get<std::string>(), "Message 2");
  EXPECT_EQ(messages[2]["content"].get<std::string>(), "Message 3");
  EXPECT_EQ(messages[3]["content"].get<std::string>(), "Tool response");
}

// Test: Compact preserves last 3 tool responses
TEST_F(HistoryTest, CompactPreservesLastThreeToolResponses) {
  // Add 5 tool responses
  for (int i = 0; i < 5; ++i) {
    history_->AddMessage(
        assistant::message{"tool", "Tool response " + std::to_string(i)},
        MessageType::kToolResponse);
  }

  history_->Compact(
      [](assistant::message& msg) { msg["content"] = "[TRIMMED]"; });

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 5);
  // Last 3 tool responses should be preserved (indices 2, 3, 4)
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "[TRIMMED]");
  EXPECT_EQ(messages[1]["content"].get<std::string>(), "[TRIMMED]");
  EXPECT_EQ(messages[2]["content"].get<std::string>(), "Tool response 2");
  EXPECT_EQ(messages[3]["content"].get<std::string>(), "Tool response 3");
  EXPECT_EQ(messages[4]["content"].get<std::string>(), "Tool response 4");
}

// Test: Compact trims tool requests but preserves tool responses
TEST_F(HistoryTest, CompactMixedMessageTypes) {
  history_->AddMessage(assistant::message{"user", "User message"},
                       MessageType::kNormal);
  history_->AddMessage(assistant::message{"tool", "Tool response 1"},
                       MessageType::kToolResponse);
  history_->AddMessage(assistant::message{"assistant", "Assistant message"},
                       MessageType::kNormal);
  history_->AddMessage(assistant::message{"tool", "Tool response 2"},
                       MessageType::kToolResponse);
  history_->AddMessage(assistant::message{"user", "Another user message"},
                       MessageType::kNormal);
  history_->AddMessage(assistant::message{"tool", "Tool response 3"},
                       MessageType::kToolResponse);

  history_->Compact(
      [](assistant::message& msg) { msg["content"] = "[TRIMMED]"; });

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 6);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "User message");
  EXPECT_EQ(messages[1]["content"].get<std::string>(), "Tool response 1");
  EXPECT_EQ(messages[2]["content"].get<std::string>(), "Assistant message");
  EXPECT_EQ(messages[3]["content"].get<std::string>(), "Tool response 2");
  EXPECT_EQ(messages[4]["content"].get<std::string>(), "Another user message");
  EXPECT_EQ(messages[5]["content"].get<std::string>(), "Tool response 3");
}

// Test: Compact with exactly 3 tool responses preserves all of them
TEST_F(HistoryTest, CompactExactlyThreeToolResponses) {
  for (int i = 0; i < 3; ++i) {
    history_->AddMessage(
        assistant::message{"tool", "Tool response " + std::to_string(i)},
        MessageType::kToolResponse);
  }

  history_->Compact(
      [](assistant::message& msg) { msg["content"] = "[TRIMMED]"; });

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 3);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Tool response 0");
  EXPECT_EQ(messages[1]["content"].get<std::string>(), "Tool response 1");
  EXPECT_EQ(messages[2]["content"].get<std::string>(), "Tool response 2");
}

// Test: Compact callback can modify message structure (simulating OllamaClient)
TEST_F(HistoryTest, CompactOllamaStyleTrimming) {
  history_->AddMessage(assistant::message{"user", "User message"},
                       MessageType::kNormal);
  history_->AddMessage(assistant::message{"tool", "Tool response content"},
                       MessageType::kToolResponse);
  history_->AddMessage(assistant::message{"assistant", "Assistant message"},
                       MessageType::kNormal);

  // Simulate OllamaClient::Compact behavior
  history_->Compact([](assistant::message& msg) {
    if (msg.contains("content") && msg["content"].is_string()) {
      msg["content"] = kTrimMessage;
    }
  });

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 3);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "User message");
  EXPECT_EQ(messages[1]["content"].get<std::string>(), "Tool response content");
  EXPECT_EQ(messages[2]["content"].get<std::string>(), "Assistant message");
}

// Test: Compact callback with array content (simulating ClaudeClient)
TEST_F(HistoryTest, CompactClaudeStyleTrimming) {
  assistant::message msg_with_array;
  msg_with_array["role"] = "user";
  msg_with_array["content"] = json::array();
  msg_with_array["content"].push_back(
      json{{"type", "text"}, {"content", "Some content here"}});

  history_->AddMessage(msg_with_array, MessageType::kNormal);
  history_->AddMessage(assistant::message{"assistant", "Assistant reply"},
                       MessageType::kNormal);

  // Simulate ClaudeClient::Compact behavior
  history_->Compact([](assistant::message& msg) {
    if (msg.contains("content") && msg["content"].is_array()) {
      auto& j_array = msg["content"];
      for (auto& element : j_array) {
        if (element.contains("content") && element["content"].is_string()) {
          element["content"] = kTrimMessage;
        }
      }
    }
  });

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 2);
  auto& content_array = messages[0]["content"];
  ASSERT_TRUE(content_array.is_array());
  ASSERT_EQ(content_array.size(), 1);
  EXPECT_EQ(content_array[0]["content"].get<std::string>(),
            "Some content here");
  EXPECT_EQ(messages[1]["content"].get<std::string>(), "Assistant reply");
}

// Test: Compact callback with output field (simulating OpenAIClient)
TEST_F(HistoryTest, CompactOpenAIStyleTrimming) {
  assistant::message msg_with_output;
  msg_with_output["role"] = "assistant";
  msg_with_output["output"] = "Some tool output here";

  history_->AddMessage(msg_with_output, MessageType::kNormal);
  history_->AddMessage(assistant::message{"user", "User message"},
                       MessageType::kNormal);

  // Simulate OpenAIClient::Compact behavior
  history_->Compact([](assistant::message& msg) {
    if (msg.contains("output") && msg["output"].is_string()) {
      msg["output"] = kTrimMessage;
    }
  });

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 2);
  EXPECT_EQ(messages[0]["output"].get<std::string>(), "Some tool output here");
  EXPECT_EQ(messages[1]["content"].get<std::string>(), "User message");
}

// Test: Compact with many tool responses only preserves last 3
TEST_F(HistoryTest, CompactManyToolResponses) {
  // Add 10 tool responses
  for (int i = 0; i < 10; ++i) {
    history_->AddMessage(
        assistant::message{"tool", "Tool response " + std::to_string(i)},
        MessageType::kToolResponse);
  }

  history_->Compact(
      [](assistant::message& msg) { msg["content"] = "[TRIMMED]"; });

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 10);
  // First 7 should be trimmed
  for (int i = 0; i < 7; ++i) {
    EXPECT_EQ(messages[i]["content"].get<std::string>(), "[TRIMMED]");
  }
  // Last 3 should be preserved
  EXPECT_EQ(messages[7]["content"].get<std::string>(), "Tool response 7");
  EXPECT_EQ(messages[8]["content"].get<std::string>(), "Tool response 8");
  EXPECT_EQ(messages[9]["content"].get<std::string>(), "Tool response 9");
}

// Test: Compact after swap to main from temp still works
TEST_F(HistoryTest, CompactAfterSwapBackToMain) {
  history_->AddMessage(assistant::message{"user", "Main message"},
                       MessageType::kNormal);
  history_->AddMessage(assistant::message{"tool", "Tool response"},
                       MessageType::kToolResponse);

  history_->SwapToTempHistory();
  history_->SwapToMainHistory();

  EXPECT_FALSE(history_->IsTempHistory());

  history_->Compact(
      [](assistant::message& msg) { msg["content"] = "[TRIMMED]"; });

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 2);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Main message");
  EXPECT_EQ(messages[1]["content"].get<std::string>(), "Tool response");
}

// Test: Compact does not affect main history when on temp
TEST_F(HistoryTest, CompactOnTempDoesNotAffectMain) {
  history_->AddMessage(assistant::message{"user", "Main message"},
                       MessageType::kNormal);
  history_->AddMessage(assistant::message{"tool", "Tool response"},
                       MessageType::kToolResponse);

  history_->SwapToTempHistory();

  // Add temp message
  history_->AddMessage(assistant::message{"user", "Temp message"},
                       MessageType::kNormal);

  // Compact on temp - should be no-op
  history_->Compact(
      [](assistant::message& msg) { msg["content"] = "[TRIMMED]"; });

  // Switch back to main
  history_->SwapToMainHistory();

  // Main history should be untouched
  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 2);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Main message");
  EXPECT_EQ(messages[1]["content"].get<std::string>(), "Tool response");
}

// Test: Thread safety - concurrent Compact and AddMessage
TEST_F(HistoryTest, ThreadSafetyConcurrentCompactAndAdd) {
  const int num_operations = 500;
  std::vector<std::thread> threads;

  // Pre-populate with some messages
  for (int i = 0; i < 100; ++i) {
    history_->AddMessage(
        assistant::message{"user", "Message " + std::to_string(i)},
        MessageType::kNormal);
  }

  threads.emplace_back([this, num_operations]() {
    for (int i = 0; i < num_operations; ++i) {
      history_->Compact(
          [](assistant::message& msg) { msg["content"] = "[TRIMMED]"; });
    }
  });

  threads.emplace_back([this, num_operations]() {
    for (int i = 0; i < num_operations; ++i) {
      history_->AddMessage(
          assistant::message{"user", "New message " + std::to_string(i)},
          MessageType::kNormal);
    }
  });

  for (auto& thread : threads) {
    thread.join();
  }

  // Test should complete without crashes or deadlocks
  SUCCEED();
}

//============================================================
// Client-specific Compact() tests
//============================================================

// Test: OllamaClient::Compact trims string content
TEST(OllamaClientCompact, TrimsStringContent) {
  OllamaClient client;

  // Add normal messages
  client.SetHistory(assistant::messages{
      assistant::message{"user", "User message"},
      assistant::message{"assistant", "Assistant response"},
  });

  client.Compact();

  auto history = client.GetHistory();
  ASSERT_EQ(history.size(), 2);
  EXPECT_EQ(history[0]["content"].get<std::string>(), "User message");
  EXPECT_EQ(history[1]["content"].get<std::string>(), "Assistant response");
}

// Test: OllamaClient::Compact preserves last 3 tool responses
TEST(OllamaClientCompact, PreservesLastThreeToolResponses) {
  OllamaClient client;

  Messages msgs;
  msgs.push_back(assistant::message{"user", "User message"},
                 MessageType::kNormal);
  msgs.push_back(assistant::message{"tool", "Tool response 1"},
                 MessageType::kToolResponse);
  msgs.push_back(assistant::message{"tool", "Tool response 2"},
                 MessageType::kToolResponse);
  msgs.push_back(assistant::message{"assistant", "Assistant message"},
                 MessageType::kNormal);
  msgs.push_back(assistant::message{"tool", "Tool response 3"},
                 MessageType::kToolResponse);
  msgs.push_back(assistant::message{"tool", "Tool response 4"},
                 MessageType::kToolResponse);
  msgs.push_back(assistant::message{"tool", "Tool response 5"},
                 MessageType::kToolResponse);

  client.SetHistory(msgs);

  client.Compact();

  auto history = client.GetHistory();
  ASSERT_EQ(history.size(), 7);
  EXPECT_EQ(history[0]["content"].get<std::string>(), "User message");
  EXPECT_EQ(history[1]["content"].get<std::string>(), kTrimMessage);
  EXPECT_EQ(history[2]["content"].get<std::string>(), kTrimMessage);
  EXPECT_EQ(history[3]["content"].get<std::string>(), "Assistant message");
  // Last 3 tool responses preserved
  EXPECT_EQ(history[4]["content"].get<std::string>(), "Tool response 3");
  EXPECT_EQ(history[5]["content"].get<std::string>(), "Tool response 4");
  EXPECT_EQ(history[6]["content"].get<std::string>(), "Tool response 5");
}

// Test: OllamaClient::Compact on empty history
TEST(OllamaClientCompact, EmptyHistory) {
  OllamaClient client;
  EXPECT_TRUE(client.GetHistory().empty());

  // Should not crash
  client.Compact();

  EXPECT_TRUE(client.GetHistory().empty());
}

// Test: ClaudeClient::Compact trims array content elements
TEST(ClaudeClientCompact, TrimsArrayContent) {
  ClaudeClient client;

  // Add a message with array content (Claude-style)
  assistant::message tool_response_msg;
  tool_response_msg["role"] = "user";
  tool_response_msg["content"] = json::array();
  tool_response_msg["content"].push_back(
      json{{"type", "text"}, {"content", "Some tool result here"}});

  // Second message also needs array content to be trimmed by ClaudeClient
  assistant::message msg_with_array2;
  msg_with_array2["role"] = "assistant";
  msg_with_array2["content"] = json::array();
  msg_with_array2["content"].push_back(
      json{{"type", "text"}, {"content", "Assistant reply"}});

  Messages msgs;
  msgs.push_back(msg_with_array2, MessageType::kNormal);
  msgs.push_back(tool_response_msg, MessageType::kToolResponse);
  msgs.push_back(tool_response_msg, MessageType::kToolResponse);
  msgs.push_back(tool_response_msg, MessageType::kToolResponse);
  msgs.push_back(tool_response_msg, MessageType::kToolResponse);
  msgs.push_back(tool_response_msg, MessageType::kToolResponse);
  msgs.push_back(msg_with_array2, MessageType::kNormal);

  client.SetHistory(msgs);
  client.Compact();

  auto history = client.GetHistory();
  ASSERT_EQ(history.size(), 7);

  auto& content_array = history[1]["content"];
  ASSERT_TRUE(content_array.is_array());
  ASSERT_EQ(content_array.size(), 1);
  EXPECT_EQ(content_array[0]["content"].get<std::string>(), kTrimMessage);

  std::array<bool, 7> modified_messages{
      false, true, true, false, false, false, false,
  };
  for (int i = 0; i < static_cast<int>(history.size()); ++i) {
    if (modified_messages[i]) {
      EXPECT_TRUE(history[i]["content"][0]["content"].get<std::string>() ==
                  kTrimMessage);

    } else {
      EXPECT_FALSE(history[i]["content"][0]["content"].get<std::string>() ==
                   kTrimMessage);
    }
  }
}

// Test: OllamaClient::Compact trims array content elements
TEST(OllamaClientCompact, TrimsArrayContent) {
  OllamaClient client;

  Messages msgs;
  // Ollama messages use string content, not Claude-style arrays.
  msgs.push_back(assistant::message{"user", "User message"},
                 MessageType::kNormal);
  msgs.push_back(assistant::message{"tool", "Tool response 1"},
                 MessageType::kToolResponse);
  msgs.push_back(assistant::message{"tool", "Tool response 2"},
                 MessageType::kToolResponse);
  msgs.push_back(assistant::message{"assistant", "Assistant message"},
                 MessageType::kNormal);
  msgs.push_back(assistant::message{"tool", "Tool response 3"},
                 MessageType::kToolResponse);
  msgs.push_back(assistant::message{"tool", "Tool response 4"},
                 MessageType::kToolResponse);
  msgs.push_back(assistant::message{"tool", "Tool response 5"},
                 MessageType::kToolResponse);

  client.SetHistory(msgs);
  client.Compact();

  auto history = client.GetHistory();
  ASSERT_EQ(history.size(), 7);

  EXPECT_EQ(history[0]["content"].get<std::string>(), "User message");
  EXPECT_EQ(history[1]["content"].get<std::string>(), kTrimMessage);
  EXPECT_EQ(history[2]["content"].get<std::string>(), kTrimMessage);
  EXPECT_EQ(history[3]["content"].get<std::string>(), "Assistant message");
  EXPECT_EQ(history[4]["content"].get<std::string>(), "Tool response 3");
  EXPECT_EQ(history[5]["content"].get<std::string>(), "Tool response 4");
  EXPECT_EQ(history[6]["content"].get<std::string>(), "Tool response 5");
}

// Test: OpenAIClient::Compact trims output field
TEST(OpenAIClientCompact, TrimsOutputField) {
  OpenAIClient client;

  // Use OpenAI-style output fields, but keep a longer history so compaction is
  // exercised.
  assistant::message msg_with_output;
  msg_with_output["role"] = "assistant";
  msg_with_output["output"] = "Some tool output here";

  assistant::message msg_with_output2;
  msg_with_output2["role"] = "user";
  msg_with_output2["output"] = "User output here";

  auto create_tool_response_message = []() {
    assistant::message tool_response;
    tool_response["type"] = "function_call_output";
    tool_response["call_id"] = "1";
    tool_response["output"] = "Tool response";
    return tool_response;
  };

  Messages msgs;
  msgs.push_back(msg_with_output, MessageType::kNormal);
  msgs.push_back(create_tool_response_message(), MessageType::kToolResponse);
  msgs.push_back(create_tool_response_message(), MessageType::kToolResponse);
  msgs.push_back(msg_with_output2, MessageType::kNormal);
  msgs.push_back(create_tool_response_message(), MessageType::kToolResponse);
  msgs.push_back(create_tool_response_message(), MessageType::kToolResponse);
  msgs.push_back(create_tool_response_message(), MessageType::kToolResponse);

  client.SetHistory(msgs);
  client.Compact();

  auto history = client.GetHistory();
  ASSERT_EQ(history.size(), 7);
  EXPECT_EQ(history[0]["output"].get<std::string>(), "Some tool output here");
  EXPECT_EQ(history[1]["output"].get<std::string>(), kTrimMessage);
  EXPECT_EQ(history[2]["output"].get<std::string>(), kTrimMessage);
  EXPECT_EQ(history[3]["output"].get<std::string>(), "User output here");
  EXPECT_EQ(history[4]["output"].get<std::string>(), "Tool response");
  EXPECT_EQ(history[5]["output"].get<std::string>(), "Tool response");
  EXPECT_EQ(history[6]["output"].get<std::string>(), "Tool response");
}

// Test: OpenAIMessagesClient::Compact trims string content
TEST(OpenAIMessagesClientCompact, TrimsStringContent) {
  OpenAIMessagesClient client;

  Messages msgs;
  msgs.push_back(assistant::message{"user", "User message"},
                 MessageType::kNormal);
  msgs.push_back(assistant::message{"tool", "Tool response 1"},
                 MessageType::kToolResponse);
  msgs.push_back(assistant::message{"tool", "Tool response 2"},
                 MessageType::kToolResponse);
  msgs.push_back(assistant::message{"assistant", "Assistant message"},
                 MessageType::kNormal);
  msgs.push_back(assistant::message{"tool", "Tool response 3"},
                 MessageType::kToolResponse);
  msgs.push_back(assistant::message{"tool", "Tool response 4"},
                 MessageType::kToolResponse);
  msgs.push_back(assistant::message{"tool", "Tool response 5"},
                 MessageType::kToolResponse);

  client.SetHistory(msgs);
  client.Compact();

  auto history = client.GetHistory();
  ASSERT_EQ(history.size(), 7);
  EXPECT_EQ(history[0]["content"].get<std::string>(), "User message");
  EXPECT_EQ(history[1]["content"].get<std::string>(), kTrimMessage);
  EXPECT_EQ(history[2]["content"].get<std::string>(), kTrimMessage);
  EXPECT_EQ(history[3]["content"].get<std::string>(), "Assistant message");
  EXPECT_EQ(history[4]["content"].get<std::string>(), "Tool response 3");
  EXPECT_EQ(history[5]["content"].get<std::string>(), "Tool response 4");
  EXPECT_EQ(history[6]["content"].get<std::string>(), "Tool response 5");
}
