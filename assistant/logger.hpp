#pragma once

#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>

namespace assistant {
enum class LogLevel { kTrace, kDebug, kInfo, kWarning, kError };

class Logger {
 public:
  static Logger& Instance() {
    static Logger instance;
    return instance;
  }

  static LogLevel FromString(const std::string& level) {
    if (level == "trace") {
      return LogLevel::kTrace;
    } else if (level == "debug") {
      return LogLevel::kDebug;
    } else if (level == "info") {
      return LogLevel::kInfo;
    } else if (level == "error") {
      return LogLevel::kError;
    } else if (level == "warn") {
      return LogLevel::kWarning;
    } else {
      return LogLevel::kInfo;
    }
  }

  void SetLogLevel(LogLevel level) {
    std::lock_guard lock{mutex_};
    level_ = level;
  }

  void SetLogFile(const std::string& filepath) {
    std::lock_guard lock{mutex_};
    std::ofstream out_file(filepath);
    if (out_file.is_open()) {
      file_ = std::move(out_file);
    }
  }

  void SetLogSink(std::function<void(LogLevel, std::string)> sink) {
    std::lock_guard lock{mutex_};
    m_log_sink = std::move(sink);
  }

  void trace(const std::stringstream& ss) { log(LogLevel::kTrace, ss); }
  void debug(const std::stringstream& ss) { log(LogLevel::kDebug, ss); }
  void info(const std::stringstream& ss) { log(LogLevel::kInfo, ss); }
  void warning(const std::stringstream& ss) { log(LogLevel::kWarning, ss); }
  void error(const std::stringstream& ss) { log(LogLevel::kError, ss); }

 private:
  Logger() : level_(LogLevel::kInfo) {}

  void log(LogLevel level, const std::stringstream& msg) {
    if (m_log_sink.has_value()) {
      // If the user provided its own sink, use it instead of the default
      // logging system.
      (*m_log_sink)(level, msg.str());
      return;
    }

    if (level < level_) {
      return;
    }

    // Add timestamp
    std::stringstream ss;
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto now_tm = std::localtime(&now_c);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    ss << std::put_time(now_tm, "%Y-%m-%d %H:%M:%S.") << std::setfill('0')
       << std::setw(3) << ms.count() << " ";

    // Add log level and colour
    if (file_.has_value()) {
      ss << GetLevelString(level);
    } else {
      switch (level) {
        case LogLevel::kTrace:
          ss << "\033[37m" << GetLevelString(level) << "\033[0m ";  // Gray
          break;
        case LogLevel::kDebug:
          ss << "\033[36m" << GetLevelString(level) << "\033[0m ";  // Cyan
          break;
        case LogLevel::kInfo:
          ss << "\033[32m" << GetLevelString(level) << "\033[0m ";  // Green
          break;
        case LogLevel::kWarning:
          ss << "\033[33m" << GetLevelString(level) << "\033[0m ";  // Yellow
          break;
        case LogLevel::kError:
          ss << "\033[31m" << GetLevelString(level) << "\033[0m ";  // Red
          break;
      }
    }

    // Add log content
    ss << msg.str();

    // Output log
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.has_value()) {
      *file_ << ss.str() << std::endl;
    } else {
      std::cerr << ss.str() << std::endl;
    }
  }

 private:
  const char* GetLevelString(LogLevel level) const {
    switch (level) {
      case LogLevel::kTrace:
        return "[TRACE]";
      case LogLevel::kDebug:
        return "[DEBUG]";
      case LogLevel::kInfo:
        return "[INFO]";
      case LogLevel::kWarning:
        return "[WARNING]";
      case LogLevel::kError:
        return "[ERROR]";
    }
    return "";
  }

  LogLevel level_;
  std::mutex mutex_;
  std::optional<std::function<void(LogLevel, std::string)>> m_log_sink{
      std::nullopt};
  std::optional<std::ofstream> file_;
};

class LogStream : public std::stringstream {
 public:
  LogStream(LogLevel level) : m_level(level) {}
  virtual ~LogStream() {
    switch (m_level) {
      case LogLevel::kTrace:
        Logger::Instance().trace(*this);
        break;
      case LogLevel::kDebug:
        Logger::Instance().debug(*this);
        break;
      case LogLevel::kInfo:
        Logger::Instance().info(*this);
        break;
      case LogLevel::kWarning:
        Logger::Instance().warning(*this);
        break;
      case LogLevel::kError:
        Logger::Instance().error(*this);
        break;
    }
  }

 private:
  LogLevel m_level{LogLevel::kInfo};
};

inline void SetLogLevel(LogLevel level) {
  Logger::Instance().SetLogLevel(level);
}

inline void SetLogFile(const std::string& filepath) {
  Logger::Instance().SetLogFile(filepath);
}

inline void SetLogSink(std::function<void(LogLevel, std::string)> sink) {
  Logger::Instance().SetLogSink(std::move(sink));
}

}  // namespace assistant

using OLogLevel = assistant::LogLevel;

#define OLOG(level) assistant::LogStream(level)
