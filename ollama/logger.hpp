#pragma once

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>

namespace ollama {
class Logger {
 public:
  static Logger& Instance() {
    static Logger instance;
    return instance;
  }

  enum class Level { kTrace, kDebug, kInfo, kWarning, kError };

  static Level FromString(const std::string& level) {
    if (level == "trace") {
      return Level::kTrace;
    } else if (level == "debug") {
      return Level::kDebug;
    } else if (level == "info") {
      return Level::kInfo;
    } else if (level == "error") {
      return Level::kError;
    } else if (level == "warn") {
      return Level::kWarning;
    } else {
      return Level::kInfo;
    }
  }

  void SetLogLevel(Level level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
  }

  void SetLogFile(const std::string& filepath) {
    std::lock_guard lock{mutex_};
    std::ofstream out_file(filepath);
    if (out_file.is_open()) {
      file_ = std::move(out_file);
    }
  }

  void trace(const std::stringstream& ss) { log(Level::kTrace, ss); }
  void debug(const std::stringstream& ss) { log(Level::kDebug, ss); }
  void info(const std::stringstream& ss) { log(Level::kInfo, ss); }
  void warning(const std::stringstream& ss) { log(Level::kWarning, ss); }
  void error(const std::stringstream& ss) { log(Level::kError, ss); }

 private:
  Logger() : level_(Level::kInfo) {}

  void log(Level level, const std::stringstream& msg) {
    if (level < level_) {
      return;
    }

    // Add timestamp
    std::stringstream ss;
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto now_tm = std::localtime(&now_c);

    ss << std::put_time(now_tm, "%Y-%m-%d %H:%M:%S") << " ";

    // Add log level and colour
    if (file_.has_value()) {
      ss << GetLevelString(level);
    } else {
      switch (level) {
        case Level::kTrace:
          ss << "\033[37m" << GetLevelString(level) << "\033[0m ";  // Gray
          break;
        case Level::kDebug:
          ss << "\033[36m" << GetLevelString(level) << "\033[0m ";  // Cyan
          break;
        case Level::kInfo:
          ss << "\033[32m" << GetLevelString(level) << "\033[0m ";  // Green
          break;
        case Level::kWarning:
          ss << "\033[33m" << GetLevelString(level) << "\033[0m ";  // Yellow
          break;
        case Level::kError:
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
  const char* GetLevelString(Level level) const {
    switch (level) {
      case Level::kTrace:
        return "[TRACE] ";
      case Level::kDebug:
        return "[DEBUG] ";
      case Level::kInfo:
        return "[INFO] ";
      case Level::kWarning:
        return "[WARNING] ";
      case Level::kError:
        return "[ERROR] ";
    }
    return "";
  }

  Level level_;
  std::mutex mutex_;
  std::optional<std::ofstream> file_;
};

class LogStream : public std::stringstream {
 public:
  LogStream(Logger::Level level) : m_level(level) {}
  virtual ~LogStream() {
    switch (m_level) {
      case Logger::Level::kTrace:
        Logger::Instance().trace(*this);
        break;
      case Logger::Level::kDebug:
        Logger::Instance().debug(*this);
        break;
      case Logger::Level::kInfo:
        Logger::Instance().info(*this);
        break;
      case Logger::Level::kWarning:
        Logger::Instance().warning(*this);
        break;
      case Logger::Level::kError:
        Logger::Instance().error(*this);
        break;
    }
  }

 private:
  Logger::Level m_level{Logger::Level::kInfo};
};

inline void LogLevel(Logger::Level level) {
  Logger::Instance().SetLogLevel(level);
}

}  // namespace ollama

using OLogLevel = ollama::Logger::Level;

#define OLOG(level) ollama::LogStream(level)
