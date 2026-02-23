#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace assistant {

struct ProcessOutput {
  bool ok{true};
  std::string out;
  std::string err;
};

// Output callback: If the callback returns `false` the launched process should
// be terminated.
using on_output_callback =
    std::function<bool(const std::string& out, const std::string& err)>;

// Completion callback for async processes
using on_process_end_callback = std::function<void(int exit_code)>;

class Process {
 public:
  /**
   * @brief Run process and wait for completion with output callback.
   *
   * If use_shell is true, the command will be executed through a shell,
   * allowing the use of shell features like pipes (|), redirections, etc.
   *
   * The output callback is invoked periodically with the captured stdout and
   * stderr. If the callback returns false, the process will be terminated.
   *
   * @param argv Command and arguments to execute. argv[0] is the command.
   * @param output_cb Callback invoked with stdout and stderr output.
   * @param use_shell If true, run command through shell (cmd.exe on Windows,
   *                  /bin/bash on Unix). Default is false.
   * @return The process exit code, or -1 on failure.
   */
  static int RunProcessAndWait(const std::vector<std::string>& argv,
                               on_output_callback output_cb,
                               bool use_shell = false);

  /**
   * @brief Executes a process and waits for it to complete, capturing its
   * output.
   *
   * This function runs a process specified by the command-line arguments, waits
   * for it to finish, and captures both standard output and standard error. If
   * the process exits with a non-zero status, the function returns an empty
   * optional.
   *
   * @param argv A vector of strings representing the command and its arguments.
   * The first element is the command name, followed by its arguments.
   * @param use_shell If true, the command is executed through a shell; if false
   * (default), the command is executed directly without shell interpretation.
   *
   * @return An optional pair of strings where the first element is the captured
   * standard output and the second element is the captured standard error.
   * Returns std::nullopt if the process exits with a non-zero status code.
   */
  static ProcessOutput RunProcessAndWait(const std::vector<std::string>& argv,
                                         bool use_shell = false) {
    std::stringstream out_stream;
    std::stringstream err_stream;
    auto output_cb = [&out_stream, &err_stream](
                         const std::string& out,
                         const std::string& err) -> bool {
      out_stream << out;
      err_stream << err;
      return true;
    };

    ProcessOutput result{
        .ok = RunProcessAndWait(argv, output_cb, use_shell) == 0,
        .out = out_stream.str(),
        .err = err_stream.str()};
    return result;
  }

  /**
   * @brief Run process asynchronously.
   *
   * If use_shell is true, the command will be executed through a shell.
   *
   * The output callback is invoked periodically with the captured stdout and
   * stderr. If the callback returns false, the process will be terminated.
   * When the process exits, the completion callback is invoked with the exit
   * code from a worker thread.
   *
   * @param argv Command and arguments to execute. argv[0] is the command.
   * @param output_cb Callback invoked with stdout and stderr output.
   * @param completion_cb Callback invoked when process completes.
   * @param use_shell If true, run command through shell (cmd.exe on Windows,
   *                  /bin/bash on Unix). Default is false.
   * @return true if the process was launched successfully, false otherwise.
   */
  static bool RunProcessAsync(const std::vector<std::string>& argv,
                              on_output_callback output_cb,
                              on_process_end_callback completion_cb,
                              bool use_shell = false);

  /**
   * @brief Terminate process with a given PID.
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

  static void EnableExecLog(bool b);
  static bool IsExecLogEnabled();
};

}  // namespace assistant
