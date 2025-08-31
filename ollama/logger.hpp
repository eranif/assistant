#pragma once

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace ollama {

enum class eLogLevel { kDebug, kInfo, kWarning, kError };

class Logger {
 public:
  static Logger& Instance() {
    static Logger instance;
    return instance;
  }

  static eLogLevel FromString(const std::string& level) {
    if (level == "debug") {
      return eLogLevel::kDebug;
    } else if (level == "info") {
      return eLogLevel::kInfo;
    } else if (level == "error") {
      return eLogLevel::kError;
    } else if (level == "warn") {
      return eLogLevel::kWarning;
    } else {
      return eLogLevel::kInfo;
    }
  }

  void SetLogLevel(eLogLevel level) {
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

  void debug(const std::stringstream& ss) { log(eLogLevel::kDebug, ss); }
  void info(const std::stringstream& ss) { log(eLogLevel::kInfo, ss); }
  void warning(const std::stringstream& ss) { log(eLogLevel::kWarning, ss); }
  void error(const std::stringstream& ss) { log(eLogLevel::kError, ss); }

 private:
  Logger() : level_(eLogLevel::kInfo) {}

  void log(eLogLevel level, const std::stringstream& msg) {
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
        case eLogLevel::kDebug:
          ss << "\033[36m" << GetLevelString(level) << "\033[0m ";  // Cyan
          break;
        case eLogLevel::kInfo:
          ss << "\033[32m" << GetLevelString(level) << "\033[0m ";  // Green
          break;
        case eLogLevel::kWarning:
          ss << "\033[33m" << GetLevelString(level) << "\033[0m ";  // Yellow
          break;
        case eLogLevel::kError:
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
  const char* GetLevelString(eLogLevel level) const {
    switch (level) {
      case eLogLevel::kDebug:
        return "[DEBUG] ";
      case eLogLevel::kInfo:
        return "[INFO] ";
      case eLogLevel::kWarning:
        return "[WARNING] ";
      case eLogLevel::kError:
        return "[ERROR] ";
    }
  }

  eLogLevel level_;
  std::mutex mutex_;
  std::optional<std::ofstream> file_;
};

class LogStream : public std::stringstream {
 public:
  LogStream(eLogLevel level) : m_level(level) {}
  virtual ~LogStream() {
    switch (m_level) {
      case eLogLevel::kDebug:
        Logger::Instance().debug(*this);
        break;
      case eLogLevel::kInfo:
        Logger::Instance().info(*this);
        break;
      case eLogLevel::kWarning:
        Logger::Instance().warning(*this);
        break;
      case eLogLevel::kError:
        Logger::Instance().error(*this);
        break;
    }
  }

 private:
  eLogLevel m_level{eLogLevel::kInfo};
};

#define LOG_DEBUG() ollama::LogStream(ollama::eLogLevel::kDebug)
#define LOG_INFO() ollama::LogStream(ollama::eLogLevel::kInfo)
#define LOG_WARNING() ollama::LogStream(ollama::eLogLevel::kWarning)
#define LOG_ERROR() ollama::LogStream(ollama::eLogLevel::kError)

inline void LogLevel(eLogLevel level) { Logger::Instance().SetLogLevel(level); }

}  // namespace ollama
