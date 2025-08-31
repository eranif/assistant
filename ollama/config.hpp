#pragma once

#include <string>

#include "ollama/mcp_local_process.hpp"

namespace ollama {

const std::string_view kServerKindStdio = "stdio";
const std::string_view kServerKindSse = "sse";

struct ServerConfig {
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

  inline const std::vector<ServerConfig>& GetServers() const {
    return m_servers;
  }

 private:
  std::vector<ServerConfig> m_servers;
};
}  // namespace ollama
