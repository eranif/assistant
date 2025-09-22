#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>

#include "ollama/helpers.hpp"
#include "ollama/mcp_local_process.hpp"
#include "ollama/model_options.hpp"

namespace ollama {

const std::string_view kServerKindStdio = "stdio";
const std::string_view kServerKindSse = "sse";

struct MCPServerConfig {
  std::string name;
  std::vector<std::string> args;
  std::optional<SSHLogin> ssh_login;
  std::optional<ollama::json> env;
  bool enabled{true};
  std::string type{kServerKindStdio};
  inline bool IsRemote() const { return ssh_login.has_value(); }
};

inline std::ostream& operator<<(std::ostream& os, const MCPServerConfig& mcp) {
  os << "MCPServerConfig {name: " << mcp.name << ", command: " << mcp.args
     << ", env: "
     << (mcp.env.has_value() ? mcp.env.value() : ollama::json::object()) << "}";
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

 private:
  Config() = default;

  std::vector<MCPServerConfig> m_servers;
  size_t m_history_size{50};
  ModelOptions m_defaultModelOptions;
  std::unordered_map<std::string, ModelOptions> m_model_options_map;
  LogLevel m_logLevel{LogLevel::kInfo};
  std::vector<std::shared_ptr<Endpoint>> endpoints_;
};
}  // namespace ollama
