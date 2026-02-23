#include "assistant/Process.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <thread>

#include "assistant/assistantlib.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace assistant {
static bool enable_exec_log{false};

void Process::EnableExecLog(bool b) { enable_exec_log = b; }
bool Process::IsExecLogEnabled() { return enable_exec_log; }

constexpr int kBufferSize = 256;
constexpr int kMaxChunkSize = 1024;
#ifdef _WIN32

namespace {

// Helper to read available data from a pipe on Windows (non-blocking)
std::string ReadAvailableFromPipe(HANDLE hPipe) {
  std::string result;
  char buffer[kBufferSize];

  while (true) {
    DWORD bytesRead = 0;
    DWORD bytesAvail = 0;
    // Check if data is available
    if (!PeekNamedPipe(hPipe, nullptr, 0, nullptr, &bytesAvail, nullptr) ||
        bytesAvail == 0) {
      break;
    }

    DWORD toRead = (bytesAvail < kBufferSize) ? bytesAvail : kBufferSize;
    memset(buffer, 0, sizeof(buffer));
    BOOL success = ReadFile(hPipe, buffer, toRead, &bytesRead, nullptr);
    if (!success || bytesRead == 0) {
      break;
    }
    result.append(buffer, bytesRead);
    if (result.size() > kMaxChunkSize) {
      break;
    }
  }

  return result;
}

// Helper to read all remaining data from a pipe on Windows
std::string ReadAllFromPipe(HANDLE hPipe) {
  std::string result;
  char buffer[kBufferSize];
  DWORD bytesRead = 0;
  memset(buffer, 0, sizeof(buffer));

  while (ReadFile(hPipe, buffer, kBufferSize, &bytesRead, nullptr) &&
         bytesRead > 0) {
    result.append(buffer, bytesRead);
    memset(buffer, 0, sizeof(buffer));
  }

  return result;
}

// Convert vector of strings to a single command line string for Windows
std::string BuildCommandLine(const std::vector<std::string>& argv) {
  if (argv.empty()) {
    return "";
  }

  std::string cmdline;
  for (size_t i = 0; i < argv.size(); ++i) {
    if (i > 0) {
      cmdline += " ";
    }

    const auto& arg = argv[i];
    cmdline += arg;
  }

  return cmdline;
}

}  // namespace

int Process::RunProcessAndWait(const std::vector<std::string>& argv,
                               on_output_callback output_cb, bool use_shell) {
  // If use_shell is true, prepend the shell command and call again with
  // use_shell = false
  if (use_shell) {
    std::vector<std::string> shell_argv = {"cmd.exe", "/c"};
    shell_argv.insert(shell_argv.end(), argv.begin(), argv.end());
    return RunProcessAndWait(shell_argv, output_cb, false);
  }

  if (argv.empty()) {
    return -1;
  }

  // Create pipes for stdout and stderr
  HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;
  HANDLE hStderrRead = nullptr, hStderrWrite = nullptr;

  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0) ||
      !SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
    return -1;
  }
  if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0) ||
      !SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0)) {
    CloseHandle(hStdoutRead);
    CloseHandle(hStdoutWrite);
    return -1;
  }

  // Set pipe to byte mode
  DWORD mode = PIPE_READMODE_BYTE;
  SetNamedPipeHandleState(hStdoutWrite, &mode, NULL, NULL);
  SetNamedPipeHandleState(hStderrRead, &mode, NULL, NULL);

  // Setup process startup info
  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.hStdError = hStderrWrite;
  si.hStdOutput = hStdoutWrite;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.dwFlags |= STARTF_USESTDHANDLES;

  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  std::string cmdline = BuildCommandLine(argv);

  // CreateProcess modifies the command line, so we need a mutable buffer
  std::vector<char> cmdlineBuf(cmdline.begin(), cmdline.end());
  cmdlineBuf.push_back('\0');

  if (Process::IsExecLogEnabled()) {
    std::cout << "\n" << cmdlineBuf.data() << std::endl;
  }

  BOOL success = CreateProcessA(nullptr,            // Application name
                                cmdlineBuf.data(),  // Command line
                                nullptr,  // Process security attributes
                                nullptr,  // Thread security attributes
                                TRUE,     // Inherit handles
                                0,        // Creation flags
                                nullptr,  // Environment
                                nullptr,  // Current directory
                                &si,      // Startup info
                                &pi       // Process information
  );

  // Close write ends of pipes in parent process
  CloseHandle(hStdoutWrite);
  CloseHandle(hStderrWrite);

  if (!success) {
    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);
    return -1;
  }

  int process_id = static_cast<int>(pi.dwProcessId);
  bool should_continue = true;

  // Poll for output while process is running
  while (true) {
    DWORD wait_result = WaitForSingleObject(pi.hProcess, 100);

    // Read available output
    std::string new_out = ReadAvailableFromPipe(hStdoutRead);
    std::string new_err = ReadAvailableFromPipe(hStderrRead);

    if (!new_out.empty() || !new_err.empty()) {
      if (output_cb) {
        should_continue = output_cb(new_out, new_err);
        if (!should_continue) {
          TerminateProcess(process_id);
          break;
        }
      }
    }

    if (wait_result == WAIT_OBJECT_0) {
      // Process has exited, read any remaining output
      std::string final_out = ReadAllFromPipe(hStdoutRead);
      std::string final_err = ReadAllFromPipe(hStderrRead);

      if (!final_out.empty() || !final_err.empty()) {
        if (output_cb) {
          output_cb(final_out, final_err);
        }
      }
      break;
    }
  }

  DWORD exitCode = 0;
  GetExitCodeProcess(pi.hProcess, &exitCode);

  // Clean up
  CloseHandle(hStdoutRead);
  CloseHandle(hStderrRead);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return static_cast<int>(exitCode);
}

bool Process::RunProcessAsync(const std::vector<std::string>& argv,
                              on_output_callback output_cb,
                              on_process_end_callback completion_cb,
                              bool use_shell) {
  // If use_shell is true, wrap the command with cmd.exe
  if (use_shell) {
    std::vector<std::string> shell_argv = {"cmd.exe", "/c"};

    // Join all arguments into a single command string
    std::string command;
    for (size_t i = 0; i < argv.size(); ++i) {
      if (i > 0) command += " ";
      command += argv[i];
    }
    shell_argv.push_back(command);

    return RunProcessAsync(shell_argv, output_cb, completion_cb, false);
  }

  if (argv.empty() || !completion_cb) {
    return false;
  }

  // Create pipes for stdout and stderr
  HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;
  HANDLE hStderrRead = nullptr, hStderrWrite = nullptr;

  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0) ||
      !SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
    return false;
  }

  if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0) ||
      !SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0)) {
    CloseHandle(hStdoutRead);
    CloseHandle(hStdoutWrite);
    return false;
  }

  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.hStdError = hStderrWrite;
  si.hStdOutput = hStdoutWrite;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.dwFlags |= STARTF_USESTDHANDLES;

  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  std::string cmdline = BuildCommandLine(argv);
  std::vector<char> cmdlineBuf(cmdline.begin(), cmdline.end());
  cmdlineBuf.push_back('\0');

  BOOL success = CreateProcessA(nullptr, cmdlineBuf.data(), nullptr, nullptr,
                                TRUE, 0, nullptr, nullptr, &si, &pi);

  CloseHandle(hStdoutWrite);
  CloseHandle(hStderrWrite);

  if (!success) {
    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);
    return false;
  }

  int pid = static_cast<int>(pi.dwProcessId);

  // Launch a thread to monitor the process
  std::thread([pi, hStdoutRead, hStderrRead, output_cb, completion_cb, pid]() {
    std::string accumulated_out;
    std::string accumulated_err;
    bool should_continue = true;

    // Poll for output while process is running
    while (true) {
      DWORD wait_result = WaitForSingleObject(pi.hProcess, 100);

      // Read available output
      std::string new_out = ReadAvailableFromPipe(hStdoutRead);
      std::string new_err = ReadAvailableFromPipe(hStderrRead);

      if (!new_out.empty() || !new_err.empty()) {
        accumulated_out += new_out;
        accumulated_err += new_err;

        if (output_cb) {
          should_continue = output_cb(accumulated_out, accumulated_err);
          if (!should_continue) {
            TerminateProcess(pid);
            break;
          }
        }
      }

      if (wait_result == WAIT_OBJECT_0) {
        // Process has exited, read any remaining output
        std::string final_out = ReadAllFromPipe(hStdoutRead);
        std::string final_err = ReadAllFromPipe(hStderrRead);

        if (!final_out.empty() || !final_err.empty()) {
          accumulated_out += final_out;
          accumulated_err += final_err;

          if (output_cb) {
            output_cb(accumulated_out, accumulated_err);
          }
        }
        break;
      }
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    completion_cb(static_cast<int>(exitCode));
  }).detach();

  return true;
}

void Process::TerminateProcess(int process_id) {
  if (process_id <= 0) {
    return;
  }

  HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, process_id);
  if (hProcess != nullptr) {
    ::TerminateProcess(hProcess, 1);
    CloseHandle(hProcess);
  }
}

bool Process::IsAlive(int process_id) {
  if (process_id <= 0) {
    return false;
  }

  HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process_id);
  if (hProcess == nullptr) {
    return false;
  }

  DWORD exitCode = 0;
  BOOL success = GetExitCodeProcess(hProcess, &exitCode);
  CloseHandle(hProcess);

  return success && exitCode == STILL_ACTIVE;
}

#else  // Unix/Linux/macOS

namespace {

// Helper to set a file descriptor to non-blocking mode
void SetNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags != -1) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

enum class WaitResult {
  kTimeout,
  kError,
  kDataAvailable,
};

WaitResult WaitOnFd(int fd) {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 5000;
  int rc = ::select(fd + 1, &fds, nullptr, nullptr, &tv);
  if (rc > 0 && FD_ISSET(fd, &fds)) {
    return WaitResult::kDataAvailable;
  }

  if (rc == 0) {
    return WaitResult::kTimeout;
  }
  return WaitResult::kError;
}

// Helper to read available data from a file descriptor (non-blocking)
std::string ReadAvailableFromFd(int fd) {
  std::string result;
  char buffer[kBufferSize];
  ssize_t bytesRead = 0;

  while ((bytesRead = read(fd, buffer, kBufferSize)) > 0) {
    result.append(buffer, bytesRead);
    if (result.size() > kMaxChunkSize) {
      break;
    }
  }

  return result;
}

// Helper to read from a file descriptor until EOF
std::string ReadFromFd(int fd) {
  std::string result;
  char buffer[kBufferSize];
  ssize_t bytesRead = 0;

  while ((bytesRead = read(fd, buffer, kBufferSize)) > 0) {
    result.append(buffer, bytesRead);
  }

  return result;
}

}  // namespace

int Process::RunProcessAndWait(const std::vector<std::string>& argv,
                               on_output_callback output_cb, bool use_shell) {
  // If use_shell is true, wrap the command with /bin/bash
  if (use_shell) {
    std::vector<std::string> shell_argv = {"/bin/bash", "-c"};

    // Join all arguments into a single command string
    std::string command;
    for (size_t i = 0; i < argv.size(); ++i) {
      if (i > 0) {
        command += " ";
      }
      command += argv[i];
    }
    shell_argv.push_back(command);

    return RunProcessAndWait(shell_argv, output_cb, false);
  }

  if (argv.empty()) {
    return -1;
  }

  // Create pipes for stdout and stderr
  int stdout_pipe[2];
  int stderr_pipe[2];

  if (pipe(stdout_pipe) != 0) {
    return -1;
  }

  if (pipe(stderr_pipe) != 0) {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    return -1;
  }

  pid_t pid = fork();

  if (pid < 0) {
    // Fork failed
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    return -1;
  }

  if (pid == 0) {
    // Child process
    // Close read ends
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    // Redirect stdout and stderr
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    // Close write ends after duplication
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Prepare arguments for execvp
    std::vector<char*> exec_argv;
    for (const auto& arg : argv) {
      exec_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    exec_argv.push_back(nullptr);

    // Execute the command
    execvp(exec_argv[0], exec_argv.data());

    // If execvp returns, it failed
    _exit(127);
  }

  // Parent process
  int process_id = static_cast<int>(pid);

  // Close write ends
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  // Set pipes to non-blocking mode
  SetNonBlocking(stdout_pipe[0]);
  SetNonBlocking(stderr_pipe[0]);

  bool should_continue = true;

  // Poll for output while process is running
  while (true) {
    // Read available output
    std::string new_out, new_err;
    auto rc = WaitOnFd(stdout_pipe[0]);
    switch (rc) {
      case WaitResult::kDataAvailable:
        new_out = ReadAvailableFromFd(stdout_pipe[0]);
        break;
      default:
        break;
    }

    rc = WaitOnFd(stderr_pipe[0]);
    switch (rc) {
      case WaitResult::kDataAvailable:
        new_err = ReadAvailableFromFd(stderr_pipe[0]);
        break;
      default:
        break;
    }

    // Always call the callback (even with no output) so it can terminate early
    if (output_cb) {
      should_continue = output_cb(new_out, new_err);
      if (!should_continue) {
        kill(pid, SIGKILL);
        break;
      }
    }

    // Check if process has exited
    int status{0};
    pid_t wait_result = waitpid(pid, &status, WNOHANG);
    if (wait_result == pid) {
      // Process has exited, read any remaining output
      std::string final_out = ReadFromFd(stdout_pipe[0]);
      std::string final_err = ReadFromFd(stderr_pipe[0]);

      if (!final_out.empty() || !final_err.empty()) {
        if (output_cb) {
          output_cb(final_out, final_err);
        }
      }

      // Close read ends
      close(stdout_pipe[0]);
      close(stderr_pipe[0]);

      if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
      } else {
        return -1;
      }
    }
  }

  // If we get here, process was terminated by callback
  close(stdout_pipe[0]);
  close(stderr_pipe[0]);

  // Wait for process to actually terminate
  int status = 0;
  waitpid(pid, &status, 0);

  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  } else {
    return -1;
  }
}

bool Process::RunProcessAsync(const std::vector<std::string>& argv,
                              on_output_callback output_cb,
                              on_process_end_callback completion_cb,
                              bool use_shell) {
  // If use_shell is true, wrap the command with /bin/bash
  if (use_shell) {
    std::vector<std::string> shell_argv = {"/bin/bash", "-c"};

    // Join all arguments into a single command string
    std::string command;
    for (size_t i = 0; i < argv.size(); ++i) {
      if (i > 0) command += " ";
      command += argv[i];
    }
    shell_argv.push_back(command);

    return RunProcessAsync(shell_argv, output_cb, completion_cb, false);
  }

  if (argv.empty() || !completion_cb) {
    return false;
  }

  // Create pipes for stdout and stderr
  int stdout_pipe[2];
  int stderr_pipe[2];

  if (pipe(stdout_pipe) != 0) {
    return false;
  }

  if (pipe(stderr_pipe) != 0) {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    return false;
  }

  pid_t pid = fork();

  if (pid < 0) {
    // Fork failed
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    return false;
  }

  if (pid == 0) {
    // Child process
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    std::vector<char*> exec_argv;
    for (const auto& arg : argv) {
      exec_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    exec_argv.push_back(nullptr);

    execvp(exec_argv[0], exec_argv.data());
    _exit(127);
  }

  // Parent process
  int process_id = static_cast<int>(pid);

  // Close write ends
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  // Launch a thread to monitor the process
  std::thread([pid, stdout_pipe, stderr_pipe, output_cb, completion_cb,
               process_id]() {
    // Set pipes to non-blocking mode
    SetNonBlocking(stdout_pipe[0]);
    SetNonBlocking(stderr_pipe[0]);

    std::string accumulated_out;
    std::string accumulated_err;
    bool should_continue = true;

    // Poll for output while process is running
    while (true) {
      // Check if process has exited
      int status = 0;
      pid_t wait_result = waitpid(pid, &status, WNOHANG);

      // Read available output
      std::string new_out = ReadAvailableFromFd(stdout_pipe[0]);
      std::string new_err = ReadAvailableFromFd(stderr_pipe[0]);

      if (!new_out.empty() || !new_err.empty()) {
        accumulated_out += new_out;
        accumulated_err += new_err;

        if (output_cb) {
          should_continue = output_cb(accumulated_out, accumulated_err);
          if (!should_continue) {
            kill(pid, SIGKILL);
            break;
          }
        }
      }

      if (wait_result == pid) {
        // Process has exited, read any remaining output
        std::string final_out = ReadFromFd(stdout_pipe[0]);
        std::string final_err = ReadFromFd(stderr_pipe[0]);

        if (!final_out.empty() || !final_err.empty()) {
          accumulated_out += final_out;
          accumulated_err += final_err;

          if (output_cb) {
            output_cb(accumulated_out, accumulated_err);
          }
        }

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        int exit_code = -1;
        if (WIFEXITED(status)) {
          exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
          exit_code = 128 + WTERMSIG(status);
        }

        completion_cb(exit_code);
        return;
      }

      // Sleep briefly before next poll
      usleep(10000);  // 10ms
    }

    // If we get here, process was terminated by callback
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    // Wait for process to actually terminate
    int status = 0;
    waitpid(pid, &status, 0);

    int exit_code = -1;
    if (WIFEXITED(status)) {
      exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      exit_code = 128 + WTERMSIG(status);
    }

    completion_cb(exit_code);
  }).detach();

  return true;
}

void Process::TerminateProcess(int process_id) {
  if (process_id <= 0) {
    return;
  }

  kill(static_cast<pid_t>(process_id), SIGTERM);
}

bool Process::IsAlive(int process_id) {
  if (process_id <= 0) {
    return false;
  }

  // Sending signal 0 checks if the process exists without killing it
  int result = kill(static_cast<pid_t>(process_id), 0);
  return result == 0;
}

#endif

}  // namespace assistant
