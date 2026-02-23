#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "assistant/Process.hpp"

using namespace assistant;

// Test running a simple command synchronously
TEST(ProcessTest, RunProcessAndWait_SimpleCommand) {
#ifdef _WIN32
  ProcessResult result =
      Process::RunProcessAndWait({"cmd", "/c", "echo", "Hello World"});
#else
  ProcessResult result = Process::RunProcessAndWait({"echo", "Hello World"});
#endif

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.process_id, -1);
  EXPECT_FALSE(result.out.empty());
  EXPECT_TRUE(result.out.find("Hello World") != std::string::npos);
}

// Test running a command that writes to stderr
TEST(ProcessTest, RunProcessAndWait_StderrOutput) {
#ifdef _WIN32
  ProcessResult result =
      Process::RunProcessAndWait({"cmd", "/c", "echo Error message 1>&2"});
#else
  ProcessResult result =
      Process::RunProcessAndWait({"sh", "-c", "echo 'Error message' >&2"});
#endif

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.process_id, -1);
  EXPECT_FALSE(result.err.empty());
  EXPECT_TRUE(result.err.find("Error message") != std::string::npos);
}

// Test running a command with non-zero exit code
TEST(ProcessTest, RunProcessAndWait_NonZeroExitCode) {
#ifdef _WIN32
  ProcessResult result =
      Process::RunProcessAndWait({"cmd", "/c", "exit", "42"});
#else
  ProcessResult result = Process::RunProcessAndWait({"sh", "-c", "exit 42"});
#endif

  EXPECT_EQ(result.exit_code, 42);
  EXPECT_NE(result.process_id, -1);
}

// Test running a command that doesn't exist.
// On Unix, fork succeeds but exec fails, so we get a valid PID but exit code
// 127. On Windows, CreateProcess may fail and return error, or succeed with
// error.
TEST(ProcessTest, RunProcessAndWait_CommandNotFound) {
  ProcessResult result =
      Process::RunProcessAndWait({"this_command_does_not_exist_12345"});

  // The important thing is that the exit code is non-zero
  EXPECT_NE(result.exit_code, 0);
  // Process ID may or may not be set depending on platform behavior
  // So we don't test for it here
}

// Test running with empty command
TEST(ProcessTest, RunProcessAndWait_EmptyCommand) {
  ProcessResult result = Process::RunProcessAndWait({});

  EXPECT_EQ(result.exit_code, -1);
  EXPECT_EQ(result.process_id, -1);
  EXPECT_FALSE(result.err.empty());
}

// Test async process execution
TEST(ProcessTest, RunProcessAsync_SimpleCommand) {
  bool callback_invoked = false;
  ProcessResult async_result;

#ifdef _WIN32
  int pid = Process::RunProcessAsync(
      {"cmd", "/c", "echo", "Async Hello"},
      [&callback_invoked, &async_result](const ProcessResult& result) {
        callback_invoked = true;
        async_result = result;
      });
#else
  int pid = Process::RunProcessAsync(
      {"echo", "Async Hello"},
      [&callback_invoked, &async_result](const ProcessResult& result) {
        callback_invoked = true;
        async_result = result;
      });
#endif

  EXPECT_NE(pid, -1);

  // Wait for the callback to be invoked (with timeout)
  for (int i = 0; i < 50 && !callback_invoked; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(async_result.exit_code, 0);
  EXPECT_FALSE(async_result.out.empty());
  EXPECT_TRUE(async_result.out.find("Async Hello") != std::string::npos);
}

// Test async process with non-zero exit code
TEST(ProcessTest, RunProcessAsync_NonZeroExitCode) {
  bool callback_invoked = false;
  ProcessResult async_result;

#ifdef _WIN32
  int pid = Process::RunProcessAsync(
      {"cmd", "/c", "exit", "17"},
      [&callback_invoked, &async_result](const ProcessResult& result) {
        callback_invoked = true;
        async_result = result;
      });
#else
  int pid = Process::RunProcessAsync(
      {"sh", "-c", "exit 17"},
      [&callback_invoked, &async_result](const ProcessResult& result) {
        callback_invoked = true;
        async_result = result;
      });
#endif

  EXPECT_NE(pid, -1);

  // Wait for the callback
  for (int i = 0; i < 50 && !callback_invoked; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(async_result.exit_code, 17);
}

// Test async with empty command
TEST(ProcessTest, RunProcessAsync_EmptyCommand) {
  bool callback_invoked = false;

  int pid = Process::RunProcessAsync(
      {}, [&callback_invoked](const ProcessResult& result) {
        callback_invoked = true;
      });

  EXPECT_EQ(pid, -1);

  // Give it a moment to ensure callback isn't invoked
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_FALSE(callback_invoked);
}

// Test IsAlive functionality
TEST(ProcessTest, IsAlive_LongRunningProcess) {
#ifdef _WIN32
  int pid =
      Process::RunProcessAsync({"cmd", "/c", "ping", "127.0.0.1", "-n", "10"},
                               [](const ProcessResult& result) {
                                 // Callback intentionally empty for this test
                               });
#else
  int pid =
      Process::RunProcessAsync({"sleep", "5"}, [](const ProcessResult& result) {
        // Callback intentionally empty for this test
      });
#endif

  EXPECT_NE(pid, -1);

  // Process should be alive immediately after starting
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_TRUE(Process::IsAlive(pid));

  // Terminate the process
  Process::TerminateProcess(pid);

  // Give it time to terminate
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Process should no longer be alive
  EXPECT_FALSE(Process::IsAlive(pid));
}

// Test IsAlive with invalid PID
TEST(ProcessTest, IsAlive_InvalidPid) {
  EXPECT_FALSE(Process::IsAlive(-1));
  EXPECT_FALSE(Process::IsAlive(0));
  EXPECT_FALSE(Process::IsAlive(999999));  // Very unlikely to exist
}

// Test TerminateProcess with invalid PID
TEST(ProcessTest, TerminateProcess_InvalidPid) {
  // These should not crash
  Process::TerminateProcess(-1);
  Process::TerminateProcess(0);
  Process::TerminateProcess(999999);  // Very unlikely to exist
}

// Test process with both stdout and stderr output
TEST(ProcessTest, RunProcessAndWait_BothOutputs) {
#ifdef _WIN32
  ProcessResult result = Process::RunProcessAndWait(
      {"cmd", "/c", "echo stdout text && echo stderr text 1>&2"});
#else
  ProcessResult result = Process::RunProcessAndWait(
      {"sh", "-c", "echo 'stdout text' && echo 'stderr text' >&2"});
#endif

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.process_id, -1);
  EXPECT_FALSE(result.out.empty());
  EXPECT_FALSE(result.err.empty());
  EXPECT_TRUE(result.out.find("stdout text") != std::string::npos);
  EXPECT_TRUE(result.err.find("stderr text") != std::string::npos);
}

// Test multiple async processes running concurrently
TEST(ProcessTest, RunProcessAsync_MultipleProcesses) {
  constexpr int kNumProcesses = 5;

  // Use a mutex and condition variable for better synchronization
  std::mutex mtx;
  std::condition_variable cv;
  int completed_count = 0;
  std::vector<int> pids;

  for (int i = 0; i < kNumProcesses; ++i) {
#ifdef _WIN32
    int pid = Process::RunProcessAsync(
        {"cmd", "/c", "echo", std::to_string(i)},
        [&mtx, &cv, &completed_count](const ProcessResult& result) {
          std::lock_guard<std::mutex> lock(mtx);
          completed_count++;
          cv.notify_one();
        });
#else
    int pid = Process::RunProcessAsync(
        {"echo", std::to_string(i)},
        [&mtx, &cv, &completed_count](const ProcessResult& result) {
          std::lock_guard<std::mutex> lock(mtx);
          completed_count++;
          cv.notify_one();
        });
#endif
    EXPECT_NE(pid, -1);
    pids.push_back(pid);
  }

  // Wait for all processes to complete
  std::unique_lock<std::mutex> lock(mtx);
  cv.wait_for(lock, std::chrono::seconds(10), [&completed_count]() {
    return completed_count >= kNumProcesses;
  });
  EXPECT_EQ(completed_count, kNumProcesses);
}

// Test process with command line arguments containing spaces
TEST(ProcessTest, RunProcessAndWait_ArgumentsWithSpaces) {
#ifdef _WIN32
  ProcessResult result = Process::RunProcessAndWait(
      {"cmd", "/c", "echo", "Hello World With Spaces"});
#else
  ProcessResult result =
      Process::RunProcessAndWait({"echo", "Hello World With Spaces"});
#endif

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(result.process_id, -1);
  EXPECT_FALSE(result.out.empty());
  EXPECT_TRUE(result.out.find("Hello World With Spaces") != std::string::npos);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
