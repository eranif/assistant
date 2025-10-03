/**
 * @file mcp_logger.h
 * @brief Simple logger
 */

#ifndef MCP_LOGGER_H
#define MCP_LOGGER_H

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

#include "assistant/logger.hpp"

namespace mcp {

enum class log_level { debug, info, warning, error };

class logger {
 public:
  static logger& instance() {
    static logger instance;
    return instance;
  }

  void set_level(log_level level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
  }

  template <typename... Args>
  void debug(Args&&... args) {
    log(log_level::debug, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void info(Args&&... args) {
    log(log_level::info, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void warning(Args&&... args) {
    log(log_level::warning, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void error(Args&&... args) {
    log(log_level::error, std::forward<Args>(args)...);
  }

 private:
  logger() : level_(log_level::info) {}

  template <typename T>
  void log_impl(std::stringstream& ss, T&& arg) {
    ss << std::forward<T>(arg);
  }

  template <typename T, typename... Args>
  void log_impl(std::stringstream& ss, T&& arg, Args&&... args) {
    ss << std::forward<T>(arg);
    log_impl(ss, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void log(log_level level, Args&&... args) {
    if (level < level_) {
      return;
    }

    std::stringstream ss;

    // Add log content
    log_impl(ss, std::forward<Args>(args)...);

    // Add log level and colour
    switch (level) {
      case log_level::debug:
        assistant::Logger::Instance().debug(ss);
        break;
      case log_level::info:
        assistant::Logger::Instance().info(ss);
        break;
      case log_level::warning:
        assistant::Logger::Instance().warning(ss);
        break;
      case log_level::error:
        assistant::Logger::Instance().error(ss);
        break;
    }
  }

  log_level level_;
  std::mutex mutex_;
};

#define MCP_LOG_DEBUG(...) mcp::logger::instance().debug(__VA_ARGS__)
#define MCP_LOG_INFO(...) mcp::logger::instance().info(__VA_ARGS__)
#define MCP_LOG_WARN(...) mcp::logger::instance().warning(__VA_ARGS__)
#define MCP_LOG_ERROR(...) mcp::logger::instance().error(__VA_ARGS__)

inline void set_log_level(log_level level) {
  mcp::logger::instance().set_level(level);
}

}  // namespace mcp

#endif  // MCP_LOGGER_H