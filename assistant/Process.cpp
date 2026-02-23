#include "assistant/Process.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <thread>

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

#ifdef _WIN32

namespace {

// Helper to read from a pipe on Windows
std::string ReadFromPipe(HANDLE hPipe) {
  std::string result;
  const DWORD kBufferSize = 4096;
  char buffer[kBufferSize];
  DWORD bytesRead = 0;

  while (true) {
    BOOL success = ReadFile(hPipe, buffer, kBufferSize, &bytesRead, nullptr);
    if (!success || bytesRead == 0) {
      break;
    }
    result.append(buffer, bytesRead);
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
    // Simple quoting: if the argument contains spaces, wrap it in quotes
    bool needsQuote = arg.find(' ') != std::string::npos ||
                      arg.find('\t') != std::string::npos;

    if (needsQuote) {
      cmdline += "\"";
      // Escape any existing quotes
      for (char ch : arg) {
        if (ch == '"') {
          cmdline += "\\\"";
        } else if (ch == '\\') {
          cmdline += "\\\\";
        } else {
          cmdline += ch;
        }
      }
      cmdline += "\"";
    } else {
      cmdline += arg;
    }
  }

  return cmdline;
}

}  // namespace

ProcessResult Process::RunProcessAndWait(const std::vector<std::string>& argv) {
  ProcessResult result;

  if (argv.empty()) {
    result.exit_code = -1;
    result.process_id = -1;
    result.err = "Empty command";
    return result;
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
    result.exit_code = -1;
    result.process_id = -1;
    result.err = "Failed to create stdout pipe";
    return result;
  }

  if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0) ||
      !SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0)) {
    CloseHandle(hStdoutRead);
    CloseHandle(hStdoutWrite);
    result.exit_code = -1;
    result.process_id = -1;
    result.err = "Failed to create stderr pipe";
    return result;
  }

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
    result.exit_code = -1;
    result.process_id = -1;
    result.err = "Failed to create process";
    return result;
  }

  result.process_id = static_cast<int>(pi.dwProcessId);

  // Read stdout and stderr
  result.out = ReadFromPipe(hStdoutRead);
  result.err = ReadFromPipe(hStderrRead);

  // Wait for process to complete
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exitCode = 0;
  GetExitCodeProcess(pi.hProcess, &exitCode);
  result.exit_code = static_cast<int>(exitCode);

  // Clean up
  CloseHandle(hStdoutRead);
  CloseHandle(hStderrRead);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return result;
}

int Process::RunProcessAsync(
    const std::vector<std::string>& argv,
    std::function<void(const ProcessResult&)> completion_cb) {
  if (argv.empty() || !completion_cb) {
    return -1;
  }

  // Launch a thread to run the process
  std::thread([argv, completion_cb]() {
    ProcessResult result = RunProcessAndWait(argv);
    completion_cb(result);
  }).detach();

  // Note: We return -1 here because we don't have the actual PID yet
  // This is a limitation of the async design. A better approach would be
  // to use synchronization to wait for the process to start.
  // For now, we'll do a simple implementation.

  // Better implementation: create the process first, get the PID, then monitor
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
    return -1;
  }

  int pid = static_cast<int>(pi.dwProcessId);

  // Launch a thread to monitor the process
  std::thread([pi, hStdoutRead, hStderrRead, completion_cb, pid]() {
    ProcessResult result;
    result.process_id = pid;

    // Read output
    result.out = ReadFromPipe(hStdoutRead);
    result.err = ReadFromPipe(hStderrRead);

    // Wait for process
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exit_code = static_cast<int>(exitCode);

    // Clean up
    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    completion_cb(result);
  }).detach();

  return pid;
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

// Helper to read from a file descriptor until EOF
std::string ReadFromFd(int fd) {
  std::string result;
  const size_t kBufferSize = 4096;
  char buffer[kBufferSize];
  ssize_t bytesRead = 0;

  while ((bytesRead = read(fd, buffer, kBufferSize)) > 0) {
    result.append(buffer, bytesRead);
  }

  return result;
}

}  // namespace

ProcessResult Process::RunProcessAndWait(const std::vector<std::string>& argv) {
  ProcessResult result;

  if (argv.empty()) {
    result.exit_code = -1;
    result.process_id = -1;
    result.err = "Empty command";
    return result;
  }

  // Create pipes for stdout and stderr
  int stdout_pipe[2];
  int stderr_pipe[2];

  if (pipe(stdout_pipe) != 0) {
    result.exit_code = -1;
    result.process_id = -1;
    result.err = "Failed to create stdout pipe";
    return result;
  }

  if (pipe(stderr_pipe) != 0) {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    result.exit_code = -1;
    result.process_id = -1;
    result.err = "Failed to create stderr pipe";
    return result;
  }

  pid_t pid = fork();

  if (pid < 0) {
    // Fork failed
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    result.exit_code = -1;
    result.process_id = -1;
    result.err = "Failed to fork process";
    return result;
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
  result.process_id = static_cast<int>(pid);

  // Close write ends
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  // Read from stdout and stderr
  result.out = ReadFromFd(stdout_pipe[0]);
  result.err = ReadFromFd(stderr_pipe[0]);

  // Close read ends
  close(stdout_pipe[0]);
  close(stderr_pipe[0]);

  // Wait for child process to complete
  int status = 0;
  waitpid(pid, &status, 0);

  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  } else {
    result.exit_code = -1;
  }

  return result;
}

int Process::RunProcessAsync(
    const std::vector<std::string>& argv,
    std::function<void(const ProcessResult&)> completion_cb) {
  if (argv.empty() || !completion_cb) {
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
  std::thread([pid, stdout_pipe, stderr_pipe, completion_cb, process_id]() {
    ProcessResult result;
    result.process_id = process_id;

    // Read output
    result.out = ReadFromFd(stdout_pipe[0]);
    result.err = ReadFromFd(stderr_pipe[0]);

    // Close read ends
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    // Wait for child process
    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
      result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      result.exit_code = 128 + WTERMSIG(status);
    } else {
      result.exit_code = -1;
    }

    completion_cb(result);
  }).detach();

  return process_id;
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
