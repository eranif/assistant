#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "assistant/client_base.hpp"

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
  history_->AddMessage(msg);

  EXPECT_FALSE(history_->IsEmpty());
  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Test");
}

// Test: Add optional message without value
TEST_F(HistoryTest, AddOptionalMessageWithoutValue) {
  std::optional<assistant::message> msg = std::nullopt;
  history_->AddMessage(msg);

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

// Test: ShrinkToFit with size larger than current
TEST_F(HistoryTest, ShrinkToFitNoChange) {
  history_->AddMessage(assistant::message{"user", "Message 1"});
  history_->AddMessage(assistant::message{"user", "Message 2"});

  history_->ShrinkToFit(5);

  auto messages = history_->GetMessages();
  EXPECT_EQ(messages.size(), 2);
}

// Test: ShrinkToFit removes old messages
TEST_F(HistoryTest, ShrinkToFitRemovesOldMessages) {
  history_->AddMessage(assistant::message{"user", "Message 1"});
  history_->AddMessage(assistant::message{"user", "Message 2"});
  history_->AddMessage(assistant::message{"user", "Message 3"});
  history_->AddMessage(assistant::message{"user", "Message 4"});
  history_->AddMessage(assistant::message{"user", "Message 5"});

  history_->ShrinkToFit(3);

  auto messages = history_->GetMessages();
  ASSERT_EQ(messages.size(), 3);
  // Should keep the last 3 messages
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Message 3");
  EXPECT_EQ(messages[1]["content"].get<std::string>(), "Message 4");
  EXPECT_EQ(messages[2]["content"].get<std::string>(), "Message 5");
}

// Test: ShrinkToFit to zero
TEST_F(HistoryTest, ShrinkToFitToZero) {
  history_->AddMessage(assistant::message{"user", "Message 1"});
  history_->AddMessage(assistant::message{"user", "Message 2"});

  history_->ShrinkToFit(0);

  EXPECT_TRUE(history_->IsEmpty());
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
  EXPECT_EQ(history_->GetMessages().size(),
            num_threads * messages_per_thread);
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

// Test: ShrinkToFit exact size
TEST_F(HistoryTest, ShrinkToFitExactSize) {
  history_->AddMessage(assistant::message{"user", "Message 1"});
  history_->AddMessage(assistant::message{"user", "Message 2"});
  history_->AddMessage(assistant::message{"user", "Message 3"});

  history_->ShrinkToFit(3);

  auto messages = history_->GetMessages();
  EXPECT_EQ(messages.size(), 3);
  EXPECT_EQ(messages[0]["content"].get<std::string>(), "Message 1");
  EXPECT_EQ(messages[1]["content"].get<std::string>(), "Message 2");
  EXPECT_EQ(messages[2]["content"].get<std::string>(), "Message 3");
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

  // Shrink temp history
  history_->ShrinkToFit(2);
  EXPECT_EQ(history_->GetMessages().size(), 2);

  // Switch back to main
  history_->SwapToMainHistory();
  EXPECT_EQ(history_->GetMessages().size(), 2);

  // Add more to main
  history_->AddMessage(assistant::message{"user", "Main 3"});
  EXPECT_EQ(history_->GetMessages().size(), 3);

  // Clear main
  history_->Clear();
  EXPECT_TRUE(history_->IsEmpty());

  // Check temp is still intact
  history_->SwapToTempHistory();
  auto temp_messages = history_->GetMessages();
  EXPECT_EQ(temp_messages.size(), 2);
  EXPECT_EQ(temp_messages[0]["content"].get<std::string>(), "Temp 2");
  EXPECT_EQ(temp_messages[1]["content"].get<std::string>(), "Temp 3");
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
