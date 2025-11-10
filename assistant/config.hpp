#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>

#include "assistant/helpers.hpp"
#include "assistant/mcp_local_process.hpp"
#include "common/magic_enum.hpp"

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
     << (mcp.env.has_value() ? mcp.env.value() : assistant::json::object())
     << "}";
  return os;
}

constexpr size_t kMaxTokensDefault = 1024;
constexpr size_t kDefaultContextSize = 32 * 1024;
constexpr std::string_view kEndpointOllamaLocal = "http://127.0.0.1:11434";
constexpr std::string_view kEndpointAnthropic = "https://api.anthropic.com";
constexpr std::string_view kEndpointOllamaCloud = "https://ollama.com";

static std::unordered_map<std::string, std::string> kDefaultOllamaHeaders = {
    {"Host", "127.0.0.1"}};

struct Endpoint {
  std::string url_{kEndpointOllamaLocal};
  EndpointKind type_{EndpointKind::ollama};
  std::unordered_map<std::string, std::string> headers_;
  bool active_{false};
  std::string model_;
  std::optional<size_t> max_tokens_{kMaxTokensDefault};
  std::optional<size_t> context_size_{kDefaultContextSize};
  bool verify_server_ssl_{true};
};

struct AnthropicEndpoint : public Endpoint {
  AnthropicEndpoint() {
    url_ = kEndpointAnthropic;
    type_ = EndpointKind::anthropic;
  }
};

struct OllamaLocalEndpoint : public Endpoint {
  OllamaLocalEndpoint() {
    url_ = kEndpointOllamaCloud;
    type_ = EndpointKind::ollama;
  }
};

struct OllamaCloudEndpoint : public Endpoint {
  OllamaCloudEndpoint() {
    url_ = kEndpointOllamaLocal;
    headers_ = kDefaultOllamaHeaders;
    type_ = EndpointKind::ollama;
  }
};

inline std::ostream& operator<<(std::ostream& os, const Endpoint& ep) {
  os << "Endpoint {url: " << ep.url_ << ", model: " << ep.model_
     << ", type: " << magic_enum::enum_name(ep.type_)
     << ", active: " << ep.active_
     << ", verify_server_ssl: " << ep.verify_server_ssl_
     << ", max_tokens=" << ep.max_tokens_.value_or(kMaxTokensDefault) << "}";
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
  Config() = default;

  static std::optional<Config> FromFile(const std::string& filepath);
  static std::optional<Config> FromContent(const std::string& json_content);

  inline const std::vector<MCPServerConfig>& GetServers() const {
    return m_servers;
  }

  void SetHistorySize(size_t history_size) { m_history_size = history_size; }
  size_t GetHistorySize() const { return m_history_size; }
  LogLevel GetLogLevel() const { return m_logLevel; }

  /// Return the active endpoint. This function may return nullptr is no
  /// endpoints are configured.
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

  /// Return the list of endpoints as defined in the configuration file.
  inline const std::vector<std::shared_ptr<Endpoint>>& GetEndpoints() const {
    return endpoints_;
  }

 private:
  std::vector<MCPServerConfig> m_servers;
  size_t m_history_size{50};
  LogLevel m_logLevel{LogLevel::kInfo};
  std::string m_keep_alive{"5m"};
  bool m_stream{true};
  ServerTimeout m_server_timeout;
  std::vector<std::shared_ptr<Endpoint>> endpoints_;
};
}  // namespace assistant
