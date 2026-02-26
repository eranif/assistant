#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>

#include "assistant/helpers.hpp"
#include "assistant/mcp.hpp"
#include "common/magic_enum.hpp"

namespace assistant {

const std::string kServerKindStdio = "stdio";
const std::string kServerKindSse = "sse";

struct StdioParams {
  std::vector<std::string> args;
  std::optional<SSHLogin> ssh_login;
  std::optional<assistant::json> env;
  inline bool IsRemote() const { return ssh_login.has_value(); }
};

struct SseParams {
  std::string baseurl;
  std::string endpoint{"/sse"};
  std::optional<std::string> auth_token;
  std::optional<assistant::json> headers;
};

struct MCPServerConfig {
  std::string name;
  bool enabled{true};
  std::optional<StdioParams> stdio_params;
  std::optional<SseParams> sse_params;
  inline bool IsStdio() const { return stdio_params.has_value(); }
  inline bool IsSse() const { return sse_params.has_value(); }
};

inline std::ostream& operator<<(std::ostream& os, const MCPServerConfig& mcp) {
  if (mcp.stdio_params.has_value()) {
    const auto& params = mcp.stdio_params.value();
    os << "MCPServerConfig(STDIO) {name: " << mcp.name
       << ", enabled: " << mcp.enabled << ", command: " << params.args;
    if (params.env.has_value()) {
      os << ", env: " << params.env.value().dump(2);
    }
    os << "}";
  } else if (mcp.sse_params.has_value()) {
    const auto& params = mcp.sse_params.value();
    os << "MCPServerConfig(SSE) {name: " << mcp.name
       << ", enabled: " << mcp.enabled << ", baseurl: " << params.baseurl
       << ", endpoint: " << params.endpoint;
    if (params.headers.has_value()) {
      os << ", headers: " << params.headers.value().dump(2);
    }
    os << "}";
  }
  return os;
}

constexpr size_t kMaxTokensDefault = 1024;
constexpr size_t kDefaultContextSize = 32 * 1024;
constexpr std::string_view kEndpointOllamaLocal = "http://127.0.0.1:11434";
constexpr std::string_view kEndpointAnthropic = "https://api.anthropic.com";
constexpr std::string_view kEndpointOllamaCloud = "https://ollama.com";
constexpr std::string_view kEndpointOpenAI = "https://api.openai.com";

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
  TransportType transport_{TransportType::httplib};
};

struct AnthropicEndpoint : public Endpoint {
  AnthropicEndpoint() {
    url_ = kEndpointAnthropic;
    type_ = EndpointKind::anthropic;
  }
};

struct OpenAIEndpoint : public Endpoint {
  OpenAIEndpoint() {
    url_ = kEndpointOpenAI;
    type_ = EndpointKind::openai;
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
  friend class ConfigBuilder;
};

struct ParseResult {
  std::string errmsg_;
  std::optional<Config> config_{std::nullopt};
  inline bool ok() const { return config_.has_value(); }
};

class ConfigBuilder {
 public:
  ~ConfigBuilder() = default;
  ConfigBuilder() = default;

  /**
   * @brief Constructs a Config object by reading and parsing a configuration
   * file.
   *
   * Attempts to open the specified file, reads its entire contents, and
   * delegates parsing to FromContent. If file access fails or parsing throws an
   * exception, an error is logged and std::nullopt is returned.
   *
   * @param filepath Path to the configuration file to load.
   * @return std::optional<Config> A populated Config object on success, or
   * std::nullopt if the file cannot be opened or parsing fails.
   *
   * @see FromContent
   */
  static ParseResult FromFile(const std::string& filepath);

  /**
   * Parses a JSON configuration string and constructs a Config object.
   *
   * This function deserializes a JSON-formatted string containing MCP server
   * configurations, endpoint definitions, timeout settings, and global options
   * into a fully-initialized Config instance. It supports two types of MCP
   * servers-stdio (local command execution, optionally over SSH) and SSE
   * (HTTP/REST-based via WebSocket-like streaming). Endpoints are validated
   * for required fields (e.g., 'model'), and exactly one endpoint is ensured
   * to be active after parsing. On any JSON parse error or configuration
   * validation failure, the function logs the issue and returns std::nullopt.
   *
   * @param content A JSON string containing the full configuration, including
   *        optional top-level keys: "mcp_servers", "endpoints",
   * "server_timeout", "history_size", "log_level", "keep_alive", and "stream".
   * @return An optional Config object populated from the input; std::nullopt if
   *         parsing fails, required fields are missing (e.g., endpoint
   * 'model'), or an invalid endpoint type is specified.
   * @throws No C++ exceptions propagate to the caller; all exceptions thrown
   *         during parsing or validation are caught internally and converted to
   *         a std::nullopt return with an error log.
   * @see Config, Endpoint, MCPServerConfig, StdioParams, SseParams
   */
  static ParseResult FromContent(const std::string& json_content);
};
}  // namespace assistant
