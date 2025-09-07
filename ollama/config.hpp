#pragma once

#include <string>
#include <unordered_map>

#include "ollama/mcp_local_process.hpp"
#include "ollama/model_options.hpp"

namespace ollama {

const std::string_view kServerKindStdio = "stdio";
const std::string_view kServerKindSse = "sse";

struct MCPServerConfig {
  std::string name;
  std::vector<std::string> args;
  std::optional<SSHLogin> ssh_login;
  bool enabled{true};
  std::string type{kServerKindStdio};
  inline bool IsRemote() const { return ssh_login.has_value(); }
};

class Config {
 public:
  ~Config() = default;
  static std::optional<Config> FromFile(const std::string& filepath);
  static std::optional<Config> FromContent(const std::string& json_content);
  static ModelOptions CreaetDefaultModelOptions();

  inline const std::vector<MCPServerConfig>& GetServers() const {
    return m_servers;
  }

  void SetUrl(const std::string& url) { m_url = url; }
  void SetHistorySize(size_t history_size) { m_history_size = history_size; }
  const std::string& GetUrl() const { return m_url; }
  size_t GetHistorySize() const { return m_history_size; }
  LogLevel GetLogLevel() const { return m_logLevel; }
  inline const std::unordered_map<std::string, ModelOptions>&
  GetModelOptionsMap() const {
    return m_model_options_map;
  }
  inline std::unordered_map<std::string, std::string> GetHeaders() const {
    return headers_;
  }

 private:
  Config() = default;

  std::vector<MCPServerConfig> m_servers;
  std::string m_url{"http://127.0.0.1:11434"};
  size_t m_history_size{50};
  ModelOptions m_defaultModelOptions;
  std::unordered_map<std::string, ModelOptions> m_model_options_map;
  LogLevel m_logLevel{LogLevel::kInfo};
  std::unordered_map<std::string, std::string> headers_;
};
}  // namespace ollama
