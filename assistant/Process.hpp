#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
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
  ~Process();

  // Non-copyable, non-movable (due to mutex and atomics)
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;
  Process(Process&&) = delete;
  Process& operator=(Process&&) = delete;

  ///===-------------------------------------------
  /// Static one-shot process API (unchanged)
  ///===-------------------------------------------

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

  ///===-------------------------------------------
  /// Static factory for interactive (bidirectional) processes
  ///===-------------------------------------------

  /**
   * @brief Start a long-lived child process with stdin kept open for writing.
   *
   * The child's stdout/stderr are read via the output callback on a background
   * thread. The process remains alive until explicitly stopped, the child
   * exits, or the returned object is destroyed.
   *
   * @param argv Command and arguments. argv[0] is the executable.
   * @param output_cb Callback invoked with stdout/stderr chunks.
   * @param use_shell If true, wrap with shell.
   * @return A shared_ptr to the interactive Process, or nullptr on failure.
   */
  static std::shared_ptr<Process> StartInteractive(
      const std::vector<std::string>& argv, on_output_callback output_cb,
      bool use_shell = false);

  /**
   * @brief Write data to the child process's stdin.
   *
   * @param data The bytes to write.
   * @return true if all bytes were written, false on error or if process is
   *         not running.
   */
  bool Write(const std::string& data);

  /**
   * @brief Write data followed by a newline to the child's stdin.
   */
  bool WriteLine(const std::string& data);

  /**
   * @brief Check if the interactive process is still alive.
   */
  bool IsRunning() const;

  /**
   * @brief Get the PID of the interactive process.
   * @return The PID, or -1 if not running.
   */
  int GetPid() const;

  /**
   * @brief Send SIGINT (or equivalent) to the child process.
   */
  void SendInterrupt();

  /**
   * @brief Stop the interactive process. Sends SIGTERM, then SIGKILL after
   *        a brief grace period if still alive.
   * @return The exit code, or -1 if the process was not running.
   */
  int Stop();

 private:
  Process() = default;

#ifdef _WIN32
  void* m_stdin_write{nullptr};     // HANDLE
  void* m_stdout_read{nullptr};     // HANDLE
  void* m_stderr_read{nullptr};     // HANDLE
  void* m_process_handle{nullptr};  // HANDLE
#else
  int m_stdin_write_fd{-1};
  int m_stdout_read_fd{-1};
  int m_stderr_read_fd{-1};
#endif
  std::atomic<int> m_child_pid{-1};
  std::atomic_bool m_running{false};
  mutable std::mutex m_write_mutex;
};

}  // namespace assistant
