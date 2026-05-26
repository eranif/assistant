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

constexpr int kBufferSize = 65536;
constexpr int kMaxChunkSize = 1048576;

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

/// Holds the handles returned after spawning a child process on Windows.
struct SpawnedProcess {
  HANDLE stdin_write{nullptr};
  HANDLE stdout_read{nullptr};
  HANDLE stderr_read{nullptr};
  HANDLE process_handle{nullptr};
  int pid{-1};
};

/// Spawn a child process with stdin/stdout/stderr pipes.
/// Returns nullopt on failure. On success the caller owns all handles.
std::optional<SpawnedProcess> SpawnProcess(
    const std::vector<std::string>& argv) {
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  HANDLE hStdinRead = nullptr, hStdinWrite = nullptr;
  HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;
  HANDLE hStderrRead = nullptr, hStderrWrite = nullptr;

  if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0) ||
      !SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0)) {
    return std::nullopt;
  }
  if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0) ||
      !SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
    CloseHandle(hStdinRead);
    CloseHandle(hStdinWrite);
    return std::nullopt;
  }
  if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0) ||
      !SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0)) {
    CloseHandle(hStdinRead);
    CloseHandle(hStdinWrite);
    CloseHandle(hStdoutRead);
    CloseHandle(hStdoutWrite);
    return std::nullopt;
  }

  DWORD mode = PIPE_READMODE_BYTE;
  SetNamedPipeHandleState(hStdoutWrite, &mode, NULL, NULL);
  SetNamedPipeHandleState(hStderrRead, &mode, NULL, NULL);

  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.hStdError = hStderrWrite;
  si.hStdOutput = hStdoutWrite;
  si.hStdInput = hStdinRead;
  si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;

  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  std::string cmdline = BuildCommandLine(argv);
  std::vector<char> cmdlineBuf(cmdline.begin(), cmdline.end());
  cmdlineBuf.push_back('\0');

  if (Process::IsExecLogEnabled()) {
    std::cout << "\n" << cmdlineBuf.data() << std::endl;
  }

  BOOL success = CreateProcessA(nullptr, cmdlineBuf.data(), nullptr, nullptr,
                                TRUE, 0, nullptr, nullptr, &si, &pi);

  // Close child-side pipe ends
  CloseHandle(hStdinRead);
  CloseHandle(hStdoutWrite);
  CloseHandle(hStderrWrite);

  if (!success) {
    CloseHandle(hStdinWrite);
    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);
    return std::nullopt;
  }

  CloseHandle(pi.hThread);

  return SpawnedProcess{
      .stdin_write = hStdinWrite,
      .stdout_read = hStdoutRead,
      .stderr_read = hStderrRead,
      .process_handle = pi.hProcess,
      .pid = static_cast<int>(pi.dwProcessId),
  };
}

}  // namespace

int Process::RunProcessAndWait(const std::vector<std::string>& argv,
                               on_output_callback output_cb, bool use_shell) {
  if (use_shell) {
    std::vector<std::string> shell_argv = {"cmd.exe", "/c"};
    shell_argv.insert(shell_argv.end(), argv.begin(), argv.end());
    return RunProcessAndWait(shell_argv, output_cb, false);
  }

  if (argv.empty()) {
    return -1;
  }

  auto spawned = SpawnProcess(argv);
  if (!spawned.has_value()) {
    return -1;
  }

  // One-shot mode: close stdin immediately so child gets EOF
  CloseHandle(spawned->stdin_write);
  spawned->stdin_write = nullptr;

  // Poll for output while process is running
  while (true) {
    std::string new_out = ReadAvailableFromPipe(spawned->stdout_read);
    std::string new_err = ReadAvailableFromPipe(spawned->stderr_read);

    if (!new_out.empty() || !new_err.empty()) {
      if (output_cb && !output_cb(new_out, new_err)) {
        TerminateProcess(spawned->pid);
        break;
      }
      continue;
    }

    DWORD wait_result = WaitForSingleObject(spawned->process_handle, 5);
    if (wait_result == WAIT_OBJECT_0) {
      std::string final_out = ReadAllFromPipe(spawned->stdout_read);
      std::string final_err = ReadAllFromPipe(spawned->stderr_read);
      if (output_cb && (!final_out.empty() || !final_err.empty())) {
        output_cb(final_out, final_err);
      }
      break;
    } else {
      if (output_cb && !output_cb("", "")) {
        TerminateProcess(spawned->pid);
        break;
      }
    }
  }

  DWORD exitCode = 0;
  GetExitCodeProcess(spawned->process_handle, &exitCode);

  CloseHandle(spawned->stdout_read);
  CloseHandle(spawned->stderr_read);
  CloseHandle(spawned->process_handle);

  return static_cast<int>(exitCode);
}

bool Process::RunProcessAsync(const std::vector<std::string>& argv,
                              on_output_callback output_cb,
                              on_process_end_callback completion_cb,
                              bool use_shell) {
  if (use_shell) {
    std::vector<std::string> shell_argv = {"cmd.exe", "/c"};
    shell_argv.push_back(BuildCommandLine(argv));
    return RunProcessAsync(shell_argv, output_cb, completion_cb, false);
  }

  if (argv.empty() || !completion_cb) {
    return false;
  }

  std::thread([argv, output_cb, completion_cb]() {
    int exit_code = RunProcessAndWait(argv, output_cb, false);
    completion_cb(exit_code);
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

std::string JoinArguments(const std::vector<std::string>& argv) {
  std::string command;
  for (size_t i = 0; i < argv.size(); ++i) {
    if (i > 0) {
      command += ' ';
    }
    command += argv[i];
  }
  return command;
}

class Argv {
  struct Deleter {
    size_t size;
    void operator()(char** argv) const {
      for (size_t i = 0; i < size; ++i) {
        delete[] argv[i];
      }
      delete[] argv;
    }
  };

  std::unique_ptr<char*[], Deleter> argv_;
  size_t size_;

 public:
  Argv(const std::vector<std::string>& args) : size_(args.size()) {
    char** argv = new char*[size_ + 1];
    for (size_t i = 0; i < size_; ++i) {
      argv[i] = new char[args[i].size() + 1];
      std::strcpy(argv[i], args[i].c_str());
    }
    argv[size_] = nullptr;
    argv_ = std::unique_ptr<char*[], Deleter>(argv, Deleter{size_});
  }

  char** get() { return argv_.get(); }
};

/// Holds the file descriptors returned after spawning a child process on Unix.
struct SpawnedProcess {
  int stdin_write_fd{-1};
  int stdout_read_fd{-1};
  int stderr_read_fd{-1};
  pid_t pid{-1};
};

/// Spawn a child process with stdin/stdout/stderr pipes.
/// Returns nullopt on failure. On success the caller owns all fds.
std::optional<SpawnedProcess> SpawnProcess(
    const std::vector<std::string>& argv) {
  int stdin_pipe[2];
  int stdout_pipe[2];
  int stderr_pipe[2];

  if (::pipe(stdin_pipe) != 0) {
    return std::nullopt;
  }
  if (::pipe(stdout_pipe) != 0) {
    ::close(stdin_pipe[0]);
    ::close(stdin_pipe[1]);
    return std::nullopt;
  }
  if (::pipe(stderr_pipe) != 0) {
    ::close(stdin_pipe[0]);
    ::close(stdin_pipe[1]);
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    return std::nullopt;
  }

  Argv exec_argv{argv};
  pid_t pid = fork();

  if (pid < 0) {
    ::close(stdin_pipe[0]);
    ::close(stdin_pipe[1]);
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[0]);
    ::close(stderr_pipe[1]);
    return std::nullopt;
  }

  if (pid == 0) {
    // Child process
    ::close(stdin_pipe[1]);
    ::close(stdout_pipe[0]);
    ::close(stderr_pipe[0]);

    ::dup2(stdin_pipe[0], STDIN_FILENO);
    ::dup2(stdout_pipe[1], STDOUT_FILENO);
    ::dup2(stderr_pipe[1], STDERR_FILENO);

    ::close(stdin_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);

    ::execvp(exec_argv.get()[0], exec_argv.get());
    ::_exit(127);
  }

  // Parent process
  ::close(stdin_pipe[0]);
  ::close(stdout_pipe[1]);
  ::close(stderr_pipe[1]);

  return SpawnedProcess{
      .stdin_write_fd = stdin_pipe[1],
      .stdout_read_fd = stdout_pipe[0],
      .stderr_read_fd = stderr_pipe[0],
      .pid = pid,
  };
}

// Helper to read from a file descriptor until EOF
std::string ReadFromFdUntilEOF(int fd) {
  std::string result;
  char buffer[kBufferSize];
  ssize_t bytesRead = 0;

  int flags = fcntl(fd, F_GETFL, 0);
  if (flags != -1) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
  while ((bytesRead = read(fd, buffer, kBufferSize)) > 0) {
    result.append(buffer, bytesRead);
  }
  return result;
}

}  // namespace

int Process::RunProcessAndWait(const std::vector<std::string>& argv,
                               on_output_callback output_cb, bool use_shell) {
  if (use_shell) {
    std::vector<std::string> shell_argv = {"/bin/bash", "-c"};
    shell_argv.push_back(JoinArguments(argv));
    return RunProcessAndWait(shell_argv, output_cb, false);
  }

  if (argv.empty()) {
    return -1;
  }

  auto spawned = SpawnProcess(argv);
  if (!spawned.has_value()) {
    return -1;
  }

  // One-shot mode: close stdin immediately so child gets EOF
  ::close(spawned->stdin_write_fd);
  spawned->stdin_write_fd = -1;

  int process_out_fd = spawned->stdout_read_fd;
  int process_err_fd = spawned->stderr_read_fd;
  bool stdout_open = true;
  bool stderr_open = true;

  while (stdout_open || stderr_open) {
    fd_set read_set;
    FD_ZERO(&read_set);

    int max_fd = -1;
    if (stdout_open) {
      FD_SET(process_out_fd, &read_set);
      max_fd = std::max(max_fd, process_out_fd);
    }
    if (stderr_open) {
      FD_SET(process_err_fd, &read_set);
      max_fd = std::max(max_fd, process_err_fd);
    }

    if (max_fd == -1) {
      break;
    }

    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000;

    int rc = ::select(max_fd + 1, &read_set, nullptr, nullptr, &tv);
    if (rc > 0) {
      std::string new_out;
      std::string new_err;

      if (stdout_open && FD_ISSET(process_out_fd, &read_set)) {
        char buffer[4096];
        ssize_t n = ::read(process_out_fd, buffer, sizeof(buffer));
        if (n > 0) {
          new_out.append(buffer, n);
        } else {
          stdout_open = false;
        }
      }

      if (stderr_open && FD_ISSET(process_err_fd, &read_set)) {
        char buffer[4096];
        ssize_t n = ::read(process_err_fd, buffer, sizeof(buffer));
        if (n > 0) {
          new_err.append(buffer, n);
        } else {
          stderr_open = false;
        }
      }

      if (output_cb && !output_cb(new_out, new_err)) {
        break;
      }
    } else if (rc == 0) {
      if (output_cb && !output_cb("", "")) {
        break;
      }
    }
  }

  if (stderr_open || stdout_open) {
    ::kill(spawned->pid, SIGKILL);
  }

  std::string out;
  if (stdout_open) {
    out = ReadFromFdUntilEOF(process_out_fd);
  }
  std::string err;
  if (stderr_open) {
    err = ReadFromFdUntilEOF(process_err_fd);
  }

  ::close(process_out_fd);
  ::close(process_err_fd);

  if (output_cb) {
    output_cb(out, err);
  }

  int status = 0;
  ::waitpid(spawned->pid, &status, 0);

  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return -1;
}

bool Process::RunProcessAsync(const std::vector<std::string>& argv,
                              on_output_callback output_cb,
                              on_process_end_callback completion_cb,
                              bool use_shell) {
  if (use_shell) {
    std::vector<std::string> shell_argv = {"/bin/bash", "-c"};
    shell_argv.push_back(JoinArguments(argv));
    return RunProcessAsync(shell_argv, output_cb, completion_cb, false);
  }

  if (argv.empty() || !completion_cb) {
    return false;
  }
  std::thread([argv, output_cb, completion_cb]() {
    int exit_code = RunProcessAndWait(argv, output_cb, false);
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

  int result = kill(static_cast<pid_t>(process_id), 0);
  return result == 0;
}

#endif

///===-------------------------------------------
/// Interactive (bidirectional) process API
///===-------------------------------------------

Process::~Process() { Stop(); }

bool Process::IsRunning() const {
  if (!m_running.load()) {
    return false;
  }
  int pid = m_child_pid.load();
  if (pid <= 0) {
    return false;
  }
  return Process::IsAlive(pid);
}

int Process::GetPid() const { return m_child_pid.load(); }

#ifdef _WIN32

std::shared_ptr<Process> Process::StartInteractive(
    const std::vector<std::string>& argv, on_output_callback output_cb,
    bool use_shell) {
  if (argv.empty()) {
    return nullptr;
  }

  if (use_shell) {
    std::vector<std::string> shell_argv = {"cmd.exe", "/c"};
    shell_argv.insert(shell_argv.end(), argv.begin(), argv.end());
    return StartInteractive(shell_argv, output_cb, false);
  }

  auto spawned = SpawnProcess(argv);
  if (!spawned.has_value()) {
    return nullptr;
  }

  auto proc = std::shared_ptr<Process>(new Process());
  proc->m_stdin_write = spawned->stdin_write;
  proc->m_stdout_read = spawned->stdout_read;
  proc->m_stderr_read = spawned->stderr_read;
  proc->m_process_handle = spawned->process_handle;
  proc->m_child_pid.store(spawned->pid);
  proc->m_running.store(true);

  std::weak_ptr<Process> weak_proc = proc;
  std::thread([weak_proc, output_cb]() {
    while (true) {
      auto proc = weak_proc.lock();
      if (!proc || !proc->m_running.load()) {
        break;
      }

      std::string out =
          ReadAvailableFromPipe(static_cast<HANDLE>(proc->m_stdout_read));
      std::string err =
          ReadAvailableFromPipe(static_cast<HANDLE>(proc->m_stderr_read));

      if (!out.empty() || !err.empty()) {
        if (output_cb) {
          output_cb(out, err);
        }
      }

      DWORD wait_result =
          WaitForSingleObject(static_cast<HANDLE>(proc->m_process_handle), 5);
      if (wait_result == WAIT_OBJECT_0) {
        std::string final_out =
            ReadAllFromPipe(static_cast<HANDLE>(proc->m_stdout_read));
        std::string final_err =
            ReadAllFromPipe(static_cast<HANDLE>(proc->m_stderr_read));
        if (output_cb && (!final_out.empty() || !final_err.empty())) {
          output_cb(final_out, final_err);
        }
        proc->m_running.store(false);
        break;
      }
    }
  }).detach();

  return proc;
}

bool Process::Write(const std::string& data) {
  std::scoped_lock lk{m_write_mutex};
  if (!m_running.load() || m_stdin_write == nullptr) {
    return false;
  }
  DWORD written = 0;
  BOOL ok = WriteFile(static_cast<HANDLE>(m_stdin_write), data.data(),
                      static_cast<DWORD>(data.size()), &written, nullptr);
  return ok && written == static_cast<DWORD>(data.size());
}

bool Process::WriteLine(const std::string& data) {
  return Write(data + "\n");
}

void Process::SendInterrupt() {
  int pid = m_child_pid.load();
  if (pid > 0 && m_running.load()) {
    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, static_cast<DWORD>(pid));
  }
}

int Process::Stop() {
  if (!m_running.load()) {
    return -1;
  }
  m_running.store(false);

  int pid = m_child_pid.load();
  if (pid > 0) {
    TerminateProcess(pid);
  }

  if (m_stdin_write) {
    CloseHandle(static_cast<HANDLE>(m_stdin_write));
    m_stdin_write = nullptr;
  }
  if (m_stdout_read) {
    CloseHandle(static_cast<HANDLE>(m_stdout_read));
    m_stdout_read = nullptr;
  }
  if (m_stderr_read) {
    CloseHandle(static_cast<HANDLE>(m_stderr_read));
    m_stderr_read = nullptr;
  }

  DWORD exitCode = 0;
  if (m_process_handle) {
    WaitForSingleObject(static_cast<HANDLE>(m_process_handle), 3000);
    GetExitCodeProcess(static_cast<HANDLE>(m_process_handle), &exitCode);
    CloseHandle(static_cast<HANDLE>(m_process_handle));
    m_process_handle = nullptr;
  }

  m_child_pid.store(-1);
  return static_cast<int>(exitCode);
}

#else  // Unix/macOS

std::shared_ptr<Process> Process::StartInteractive(
    const std::vector<std::string>& argv, on_output_callback output_cb,
    bool use_shell) {
  if (argv.empty()) {
    return nullptr;
  }

  if (use_shell) {
    std::vector<std::string> shell_argv = {"/bin/bash", "-c"};
    shell_argv.push_back(JoinArguments(argv));
    return StartInteractive(shell_argv, output_cb, false);
  }

  auto spawned = SpawnProcess(argv);
  if (!spawned.has_value()) {
    return nullptr;
  }

  auto proc = std::shared_ptr<Process>(new Process());
  proc->m_stdin_write_fd = spawned->stdin_write_fd;
  proc->m_stdout_read_fd = spawned->stdout_read_fd;
  proc->m_stderr_read_fd = spawned->stderr_read_fd;
  proc->m_child_pid.store(static_cast<int>(spawned->pid));
  proc->m_running.store(true);

  std::weak_ptr<Process> weak_proc = proc;
  std::thread([weak_proc, output_cb]() {
    bool stdout_open = true;
    bool stderr_open = true;

    while (stdout_open || stderr_open) {
      auto proc = weak_proc.lock();
      if (!proc || !proc->m_running.load()) {
        break;
      }

      fd_set read_set;
      FD_ZERO(&read_set);

      int max_fd = -1;
      if (stdout_open && proc->m_stdout_read_fd >= 0) {
        FD_SET(proc->m_stdout_read_fd, &read_set);
        max_fd = std::max(max_fd, proc->m_stdout_read_fd);
      }
      if (stderr_open && proc->m_stderr_read_fd >= 0) {
        FD_SET(proc->m_stderr_read_fd, &read_set);
        max_fd = std::max(max_fd, proc->m_stderr_read_fd);
      }

      if (max_fd == -1) {
        break;
      }

      timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 10000;

      int rc = ::select(max_fd + 1, &read_set, nullptr, nullptr, &tv);
      if (rc > 0) {
        std::string new_out;
        std::string new_err;

        if (stdout_open && FD_ISSET(proc->m_stdout_read_fd, &read_set)) {
          char buffer[4096];
          ssize_t n = ::read(proc->m_stdout_read_fd, buffer, sizeof(buffer));
          if (n > 0) {
            new_out.append(buffer, n);
          } else {
            stdout_open = false;
          }
        }

        if (stderr_open && FD_ISSET(proc->m_stderr_read_fd, &read_set)) {
          char buffer[4096];
          ssize_t n = ::read(proc->m_stderr_read_fd, buffer, sizeof(buffer));
          if (n > 0) {
            new_err.append(buffer, n);
          } else {
            stderr_open = false;
          }
        }

        if (output_cb && (!new_out.empty() || !new_err.empty())) {
          output_cb(new_out, new_err);
        }
      }
    }

    if (auto proc = weak_proc.lock()) {
      proc->m_running.store(false);
    }
  }).detach();

  return proc;
}

bool Process::Write(const std::string& data) {
  std::scoped_lock lk{m_write_mutex};
  if (!m_running.load() || m_stdin_write_fd < 0) {
    return false;
  }

  const char* ptr = data.data();
  size_t remaining = data.size();
  while (remaining > 0) {
    ssize_t written = ::write(m_stdin_write_fd, ptr, remaining);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    ptr += written;
    remaining -= static_cast<size_t>(written);
  }
  return true;
}

bool Process::WriteLine(const std::string& data) { return Write(data + "\n"); }

void Process::SendInterrupt() {
  int pid = m_child_pid.load();
  if (pid > 0 && m_running.load()) {
    ::kill(static_cast<pid_t>(pid), SIGINT);
  }
}

int Process::Stop() {
  if (!m_running.load()) {
    return -1;
  }
  m_running.store(false);

  int pid = m_child_pid.load();
  if (pid <= 0) {
    return -1;
  }

  if (m_stdin_write_fd >= 0) {
    ::close(m_stdin_write_fd);
    m_stdin_write_fd = -1;
  }

  ::kill(static_cast<pid_t>(pid), SIGTERM);

  int status = 0;
  int wait_result = 0;

  for (int i = 0; i < 30; ++i) {
    wait_result = ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
    if (wait_result != 0) {
      break;
    }
    usleep(100000);
  }

  if (wait_result == 0) {
    ::kill(static_cast<pid_t>(pid), SIGKILL);
    ::waitpid(static_cast<pid_t>(pid), &status, 0);
  }

  if (m_stdout_read_fd >= 0) {
    ::close(m_stdout_read_fd);
    m_stdout_read_fd = -1;
  }
  if (m_stderr_read_fd >= 0) {
    ::close(m_stderr_read_fd);
    m_stderr_read_fd = -1;
  }

  m_child_pid.store(-1);

  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return -1;
}

#endif

}  // namespace assistant
