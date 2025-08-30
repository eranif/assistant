#pragma once

#include <chrono>
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

  void set_level(eLogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
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

    // Add log level and color
    switch (level) {
      case eLogLevel::kDebug:
        ss << "\033[36m[DEBUG]\033[0m ";  // Cyan
        break;
      case eLogLevel::kInfo:
        ss << "\033[32m[INFO]\033[0m ";  // Green
        break;
      case eLogLevel::kWarning:
        ss << "\033[33m[WARNING]\033[0m ";  // Yellow
        break;
      case eLogLevel::kError:
        ss << "\033[31m[ERROR]\033[0m ";  // Red
        break;
    }

    // Add log content
    ss << msg.str();

    // Output log
    std::lock_guard<std::mutex> lock(mutex_);
    std::cerr << ss.str() << std::endl;
  }

  eLogLevel level_;
  std::mutex mutex_;
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

#define LG_DEBUG() ollama::LogStream(ollama::eLogLevel::kDebug)
#define LG_INFO() ollama::LogStream(ollama::eLogLevel::kInfo)
#define LG_WARN() ollama::LogStream(ollama::eLogLevel::kWarning)
#define LG_ERROR() ollama::LogStream(ollama::eLogLevel::kError)

inline void LogLevel(eLogLevel level) { Logger::Instance().set_level(level); }

}  // namespace ollama
