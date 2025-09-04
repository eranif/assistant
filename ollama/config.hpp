#pragma once

#include <string>

#include "ollama/mcp_local_process.hpp"

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
  Config(const std::string& filepath);
  ~Config() = default;

  inline const std::vector<MCPServerConfig>& GetServers() const {
    return m_servers;
  }

  void SetUrl(const std::string& url) { m_url = url; }
  void SetUseGpu(bool use_gpu) { m_use_gpu = use_gpu; }
  void SetHistorySize(size_t history_size) { m_history_size = history_size; }
  void SetContextSize(size_t context_size) { m_context_size = context_size; }
  const std::string& GetUrl() const { return m_url; }
  bool IsUseGpu() const { return m_use_gpu; }
  size_t GetHistorySize() const { return m_history_size; }
  size_t GetContextSize() const { return m_context_size; }

 private:
  std::vector<MCPServerConfig> m_servers;
  std::string m_url{"http://127.0.0.1:11434"};
  bool m_use_gpu{true};
  size_t m_history_size{20};
  size_t m_context_size{32768};
};
}  // namespace ollama
