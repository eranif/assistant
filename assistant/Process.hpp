#pragma once

#include <functional>
#include <string>
#include <vector>

namespace assistant {

struct ProcessResult {
  std::string err;     // captured stderr
  std::string out;     // captured stdout
  int exit_code{0};    // the process exit code
  int process_id{-1};  // the process ID
};

class Process {
 public:
  /**
   * @brief Run process and wait for completion. Synchronous method.
   *
   * If use_shell is true, the command will be executed through a shell,
   * allowing the use of shell features like pipes (|), redirections, etc.
   *
   * Launches a process with the given arguments and waits for it to complete.
   * Captures stdout and stderr streams. The first element of argv is the
   * executable path/name, and subsequent elements are the arguments.
   *
   * @param argv Command and arguments to execute. argv[0] is the command.
   * @return ProcessResult containing captured output, error, exit code, and
   * process ID.
   *
   * @param use_shell If true, run command through shell (cmd.exe on Windows,
   *                  /bin/bash on Unix). Default is false.
   *
   * @throws None. Returns a ProcessResult with exit_code=-1 and process_id=-1
   *         on failure.
   */
  static ProcessResult RunProcessAndWait(const std::vector<std::string>& argv,
                                         bool use_shell = false);

  /**
   * @brief Run process asynchronously.
   *
   * If use_shell is true, the command will be executed through a shell.
   *
   * Launches a process with the given arguments and returns immediately.
   * When the process exits, the completion_cb callback is invoked with the
   * process result. The callback is invoked from a worker thread.
   *
   * @param argv Command and arguments to execute. argv[0] is the command.
   * @param completion_cb Callback function invoked when process completes.
   * @return The process ID on success, or -1 in case of process start error.
   *
   * @param use_shell If true, run command through shell (cmd.exe on Windows,
   *                  /bin/bash on Unix). Default is false.
   *
   * @throws None. Returns -1 on failure.
   */
  static int RunProcessAsync(
      const std::vector<std::string>& argv,
      std::function<void(const ProcessResult&)> completion_cb,
      bool use_shell = false);

  /**
   * @brief Terminate process with a given PID.
   *
   * Terminates the process identified by process_id. On Unix-like systems,
   * sends SIGTERM. On Windows, calls TerminateProcess.
   *
   * @param process_id The process ID to terminate.
   *
   * @throws None.
   */
  static void TerminateProcess(int process_id);

  /**
   * @brief Check if a process is alive.
   *
   * Checks whether the process identified by process_id is still running.
   *
   * @param process_id The process ID to check.
   * @return true if the process is alive, false otherwise.
   *
   * @throws None.
   */
  static bool IsAlive(int process_id);
};

}  // namespace assistant
