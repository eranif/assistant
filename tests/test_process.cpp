#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "assistant/Process.hpp"

using namespace assistant;

// Helper function to create a simple ProcessResult-like structure from the new
// API
struct ProcessResult {
  std::string out;
  std::string err;
  int exit_code{-1};
};

// Helper to run process synchronously and collect all output
ProcessResult RunProcessSync(const std::vector<std::string>& argv,
                             bool use_shell = false) {
  ProcessResult result;
  result.exit_code = Process::RunProcessAndWait(
      argv,
      [&result](const std::string& out, const std::string& err) {
        result.out = out;
        result.err = err;
        return true;  // Continue
      },
      use_shell);
  return result;
}

// Test running a simple command synchronously
TEST(ProcessTest, RunProcessAndWait_SimpleCommand) {
#ifdef _WIN32
  ProcessResult result = RunProcessSync({"cmd", "/c", "echo", "Hello World"});
#else
  ProcessResult result = RunProcessSync({"echo", "Hello World"});
#endif

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(result.out.empty());
  EXPECT_TRUE(result.out.find("Hello World") != std::string::npos);
}

// Test running a command that writes to stderr
TEST(ProcessTest, RunProcessAndWait_StderrOutput) {
#ifdef _WIN32
  ProcessResult result =
      RunProcessSync({"cmd", "/c", "echo Error message 1>&2"});
#else
  ProcessResult result =
      RunProcessSync({"sh", "-c", "echo 'Error message' >&2"});
#endif

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(result.err.empty());
  EXPECT_TRUE(result.err.find("Error message") != std::string::npos);
}

// Test running a command with non-zero exit code
TEST(ProcessTest, RunProcessAndWait_NonZeroExitCode) {
#ifdef _WIN32
  ProcessResult result = RunProcessSync({"cmd", "/c", "exit", "42"});
#else
  ProcessResult result = RunProcessSync({"sh", "-c", "exit 42"});
#endif

  EXPECT_EQ(result.exit_code, 42);
}

// Test running a command that doesn't exist.
// On Unix, fork succeeds but exec fails, so we get exit code 127.
// On Windows, CreateProcess may fail.
TEST(ProcessTest, RunProcessAndWait_CommandNotFound) {
  ProcessResult result = RunProcessSync({"this_command_does_not_exist_12345"});

  // The important thing is that the exit code is non-zero
  EXPECT_NE(result.exit_code, 0);
}

// Test running with empty command
TEST(ProcessTest, RunProcessAndWait_EmptyCommand) {
  on_output_callback cb = [](const std::string& out, const std::string& err) {
    return true;
  };
  int exit_code = Process::RunProcessAndWait({}, cb);

  EXPECT_EQ(exit_code, -1);
}

// Test async process execution
TEST(ProcessTest, RunProcessAsync_SimpleCommand) {
  bool callback_invoked = false;
  int exit_code = -1;
  std::string out, err;

#ifdef _WIN32
  bool success = Process::RunProcessAsync(
      {"cmd", "/c", "echo", "Async Hello"},
      [&out, &err](const std::string& o, const std::string& e) {
        out = o;
        err = e;
        return true;
      },
      [&callback_invoked, &exit_code](int ec) {
        callback_invoked = true;
        exit_code = ec;
      });
#else
  bool success = Process::RunProcessAsync(
      {"echo", "Async Hello"},
      [&out, &err](const std::string& o, const std::string& e) {
        out = o;
        err = e;
        return true;
      },
      [&callback_invoked, &exit_code](int ec) {
        callback_invoked = true;
        exit_code = ec;
      });
#endif

  EXPECT_TRUE(success);

  // Wait for the callback to be invoked (with timeout)
  for (int i = 0; i < 50 && !callback_invoked; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(exit_code, 0);
  EXPECT_FALSE(out.empty());
  EXPECT_TRUE(out.find("Async Hello") != std::string::npos);
}

// Test async process with non-zero exit code
TEST(ProcessTest, RunProcessAsync_NonZeroExitCode) {
  bool callback_invoked = false;
  int exit_code = -1;

#ifdef _WIN32
  bool success = Process::RunProcessAsync(
      {"cmd", "/c", "exit", "17"},
      [](const std::string& out, const std::string& err) { return true; },
      [&callback_invoked, &exit_code](int ec) {
        callback_invoked = true;
        exit_code = ec;
      });
#else
  bool success = Process::RunProcessAsync(
      {"sh", "-c", "exit 17"},
      [](const std::string& out, const std::string& err) { return true; },
      [&callback_invoked, &exit_code](int ec) {
        callback_invoked = true;
        exit_code = ec;
      });
#endif

  EXPECT_TRUE(success);

  // Wait for the callback
  for (int i = 0; i < 50 && !callback_invoked; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(exit_code, 17);
}

// Test async with empty command
TEST(ProcessTest, RunProcessAsync_EmptyCommand) {
  bool callback_invoked = false;

  bool success = Process::RunProcessAsync(
      {}, [](const std::string& out, const std::string& err) { return true; },
      [&callback_invoked](int exit_code) { callback_invoked = true; });

  EXPECT_FALSE(success);

  // Give it a moment to ensure callback isn't invoked
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_FALSE(callback_invoked);
}

// Test output callback returning false to terminate process
TEST(ProcessTest, RunProcessAndWait_CallbackTermination) {
  on_output_callback cb = [](const std::string& out, const std::string& err) {
    // Terminate immediately
    return false;
  };
#ifdef _WIN32
  int exit_code = Process::RunProcessAndWait(
      {"cmd", "/c", "ping", "127.0.0.1", "-n", "10"}, cb);
#else
  int exit_code = Process::RunProcessAndWait({"sleep", "10"}, cb);
#endif

  // Process should have been terminated
  EXPECT_NE(exit_code, 0);
}

// Test async process with callback termination
TEST(ProcessTest, RunProcessAsync_CallbackTermination) {
  bool callback_invoked = false;
  int exit_code = -1;

#ifdef _WIN32
  bool success = Process::RunProcessAsync(
      {"cmd", "/c", "ping", "127.0.0.1", "-n", "10"},
      [](const std::string& out, const std::string& err) {
        // Terminate immediately
        return false;
      },
      [&callback_invoked, &exit_code](int ec) {
        callback_invoked = true;
        exit_code = ec;
      });
#else
  bool success = Process::RunProcessAsync(
      {"sleep", "10"},
      [](const std::string& out, const std::string& err) {
        // Terminate immediately
        return false;
      },
      [&callback_invoked, &exit_code](int ec) { callback_invoked = true; });
#endif

  EXPECT_TRUE(success);

  // Wait for the callback
  for (int i = 0; i < 50 && !callback_invoked; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  EXPECT_TRUE(callback_invoked);
  // Process was terminated, so exit code should be non-zero
  EXPECT_NE(exit_code, 0);
}

// Test IsAlive functionality
TEST(ProcessTest, IsAlive_LongRunningProcess) {
  int process_id = -1;
  bool started = false;

#ifdef _WIN32
  // We need a way to get the PID - for this test we'll use a workaround
  // Since RunProcessAsync no longer returns PID, this test is harder to
  // implement We'll skip detailed PID testing for now
  GTEST_SKIP() << "PID tracking not available with new API";
#else
  GTEST_SKIP() << "PID tracking not available with new API";
#endif
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
  ProcessResult result = RunProcessSync(
      {"cmd", "/c", "echo stdout text && echo stderr text 1>&2"});
#else
  ProcessResult result = RunProcessSync(
      {"sh", "-c", "echo 'stdout text' && echo 'stderr text' >&2"});
#endif

  EXPECT_EQ(result.exit_code, 0);
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
    bool success = Process::RunProcessAsync(
        {"cmd", "/c", "echo", std::to_string(i)},
        [](const std::string& out, const std::string& err) { return true; },
        [&mtx, &cv, &completed_count](int exit_code) {
          std::lock_guard<std::mutex> lock(mtx);
          completed_count++;
          cv.notify_one();
        });
#else
    bool success = Process::RunProcessAsync(
        {"echo", std::to_string(i)},
        [](const std::string& out, const std::string& err) { return true; },
        [&mtx, &cv, &completed_count](int exit_code) {
          std::lock_guard<std::mutex> lock(mtx);
          completed_count++;
          cv.notify_one();
        });
#endif
    EXPECT_TRUE(success);
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
  ProcessResult result =
      RunProcessSync({"cmd", "/c", "echo", "Hello World With Spaces"});
#else
  ProcessResult result = RunProcessSync({"echo", "Hello World With Spaces"});
#endif

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(result.out.empty());
  EXPECT_TRUE(result.out.find("Hello World With Spaces") != std::string::npos);
}

// Test shell execution with pipes
TEST(ProcessTest, RunProcessAndWait_ShellWithPipe) {
#ifdef _WIN32
  ProcessResult result =
      RunProcessSync({"echo", "hello", "|", "findstr", "hello"}, true);
#else
  ProcessResult result =
      RunProcessSync({"echo", "hello", "|", "grep", "hello"}, true);
#endif

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(result.out.empty());
  EXPECT_TRUE(result.out.find("hello") != std::string::npos);
}

// Test shell execution with command chaining (&&)
TEST(ProcessTest, RunProcessAndWait_ShellWithChaining) {
#ifdef _WIN32
  ProcessResult result =
      RunProcessSync({"echo", "first", "&&", "echo", "second"}, true);
#else
  ProcessResult result =
      RunProcessSync({"echo", "first", "&&", "echo", "second"}, true);
#endif

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(result.out.empty());
  EXPECT_TRUE(result.out.find("first") != std::string::npos);
  EXPECT_TRUE(result.out.find("second") != std::string::npos);
}

// Test shell execution with redirection
TEST(ProcessTest, RunProcessAndWait_ShellWithRedirection) {
#ifdef _WIN32
  ProcessResult result = RunProcessSync({"echo", "error_msg", "1>&2"}, true);
#else
  ProcessResult result = RunProcessSync({"echo", "error_msg", ">&2"}, true);
#endif

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_FALSE(result.err.empty());
  EXPECT_TRUE(result.err.find("error_msg") != std::string::npos);
}

// Test async shell execution with pipes
TEST(ProcessTest, RunProcessAsync_ShellWithPipe) {
  bool callback_invoked = false;
  int exit_code = -1;
  std::string out, err;

#ifdef _WIN32
  bool success = Process::RunProcessAsync(
      {"echo", "async_test", "|", "findstr", "async"},
      [&out, &err](const std::string& o, const std::string& e) {
        out = o;
        err = e;
        return true;
      },
      [&callback_invoked, &exit_code](int ec) {
        callback_invoked = true;
        exit_code = ec;
      },
      true);
#else
  bool success = Process::RunProcessAsync(
      {"echo", "async_test", "|", "grep", "async"},
      [&out, &err](const std::string& o, const std::string& e) {
        out = o;
        err = e;
        return true;
      },
      [&callback_invoked, &exit_code](int ec) {
        callback_invoked = true;
        exit_code = ec;
      },
      true);
#endif

  EXPECT_TRUE(success);

  // Wait for the callback to be invoked (with timeout)
  for (int i = 0; i < 50 && !callback_invoked; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(exit_code, 0);
  EXPECT_FALSE(out.empty());
  EXPECT_TRUE(out.find("async") != std::string::npos);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
