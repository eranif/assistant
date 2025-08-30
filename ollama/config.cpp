#include "ollama/config.hpp"

#include <fstream>

#include "ollama/logger.hpp"

namespace ollama {
Config::Config(const std::string& filepath) {
  try {
    std::ifstream input_file(filepath);
    if (!input_file.is_open()) {
      LG_ERROR() << "Failed to open file: " << filepath;
      return;
    }
    json parsed_data = json::parse(input_file);
    auto servers = parsed_data["servers"];
    for (const auto& server : servers) {
      ServerConfig server_config;
      server_config.name = server["name"];
      server_config.args = server["command"];
      if (server.contains("ssh")) {
        auto j_ssh_login = server["ssh"];
        SSHLogin ssh_login;
        ssh_login.hostname = j_ssh_login["hostname"];
        if (j_ssh_login.contains("ssh_program")) {
          ssh_login.ssh_program = j_ssh_login["ssh_program"];
        }
        if (j_ssh_login.contains("key")) {
          ssh_login.ssh_key = j_ssh_login["key"];
        }
        if (j_ssh_login.contains("user")) {
          ssh_login.user = j_ssh_login["user"];
        }
        if (j_ssh_login.contains("port")) {
          ssh_login.port = j_ssh_login["port"];
        }
        server_config.ssh_login = std::move(ssh_login);
      }
      m_servers.push_back(std::move(server_config));
    }
    LG_INFO() << "Successfully loaded " << m_servers.size()
              << " configurations";
  } catch (std::exception& e) {
    LG_ERROR() << "Failed to parse configuration file: " << filepath << ". "
               << e.what();
  }
}

}  // namespace ollama