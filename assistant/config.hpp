#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>

#include "assistant/helpers.hpp"
#include "assistant/mcp_local_process.hpp"
#include "assistant/model_options.hpp"

namespace assistant {

const std::string_view kServerKindStdio = "stdio";
const std::string_view kServerKindSse = "sse";

struct MCPServerConfig {
  std::string name;
  std::vector<std::string> args;
  std::optional<SSHLogin> ssh_login;
  std::optional<assistant::json> env;
  bool enabled{true};
  std::string type{kServerKindStdio};
  inline bool IsRemote() const { return ssh_login.has_value(); }
};

inline std::ostream& operator<<(std::ostream& os, const MCPServerConfig& mcp) {
  os << "MCPServerConfig {name: " << mcp.name << ", command: " << mcp.args
     << ", env: "
     << (mcp.env.has_value() ? mcp.env.value() : assistant::json::object()) << "}";
  return os;
}

struct Endpoint {
  std::string url_;
  std::string type_{"ollama"};
  std::unordered_map<std::string, std::string> headers_;
  bool active_{false};

  inline std::unordered_map<std::string, std::string> GetHeaders() const {
    return headers_;
  }

  inline const std::string& GetUrl() const { return url_; }
};

inline std::ostream& operator<<(std::ostream& os, const Endpoint& ep) {
  os << "Endpoint {url: " << ep.url_ << ", type: " << ep.type_
     << ", active: " << ep.active_ << "}";
  return os;
}

struct ServerTimeout {
  /// Timeout for connecting to the server, milliseconds.
  int connect_ms_{100};
  /// Timeout for reading from the server, milliseconds.
  int read_ms_{10000};
  /// Timeout for writing to the server, milliseconds.
  int write_ms_{10000};

  /// Convert milliseconds to pair of secs/micros.
  inline std::pair<int, int> ToSecsAndMicros(int time_ms) const {
    int secs = time_ms / 1000;
    int micro_secs = (time_ms % 1000) * 1000;
    return {secs, micro_secs};
  }

  inline std::pair<int, int> GetConnectTimeout() const {
    return ToSecsAndMicros(connect_ms_);
  }

  inline std::pair<int, int> GetReadTimeout() const {
    return ToSecsAndMicros(read_ms_);
  }

  inline std::pair<int, int> GetWriteTimeout() const {
    return ToSecsAndMicros(write_ms_);
  }
};

inline std::ostream& operator<<(std::ostream& os, const ServerTimeout& t) {
  os << "Timeout {connect: " << t.connect_ms_ << "ms, read: " << t.read_ms_
     << "ms, write: " << t.write_ms_ << "ms}";
  return os;
}

class Config {
 public:
  ~Config() = default;
  static std::optional<Config> FromFile(const std::string& filepath);
  static std::optional<Config> FromContent(const std::string& json_content);
  static ModelOptions CreaetDefaultModelOptions();

  inline const std::vector<MCPServerConfig>& GetServers() const {
    return m_servers;
  }

  void SetHistorySize(size_t history_size) { m_history_size = history_size; }
  size_t GetHistorySize() const { return m_history_size; }
  LogLevel GetLogLevel() const { return m_logLevel; }
  inline const std::unordered_map<std::string, ModelOptions>&
  GetModelOptionsMap() const {
    return m_model_options_map;
  }

  /// Return the active endpoint.
  inline std::shared_ptr<Endpoint> GetEndpoint() const {
    auto iter =
        std::find_if(endpoints_.begin(), endpoints_.end(),
                     [](std::shared_ptr<Endpoint> ep) { return ep->active_; });
    if (iter != endpoints_.end()) {
      return *iter;
    }

    // Return the first one.
    if (!endpoints_.empty()) {
      return *endpoints_.begin();
    }
    return nullptr;
  }

  const std::string& GetKeepAlive() const { return m_keep_alive; }
  bool IsStream() const { return m_stream; }
  inline ServerTimeout GetServerTimeoutSettings() const {
    return m_server_timeout;
  }

 private:
  Config() = default;

  std::vector<MCPServerConfig> m_servers;
  size_t m_history_size{50};
  ModelOptions m_defaultModelOptions;
  std::unordered_map<std::string, ModelOptions> m_model_options_map;
  LogLevel m_logLevel{LogLevel::kInfo};
  std::string m_keep_alive{"5m"};
  bool m_stream{true};
  ServerTimeout m_server_timeout;
  std::vector<std::shared_ptr<Endpoint>> endpoints_;
};
}  // namespace assistant
