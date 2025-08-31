#include "ollama/config.hpp"

#include <fstream>

#include "ollama/logger.hpp"

namespace ollama {
namespace {
template <typename T>
std::optional<T> GetValueFromJson(const json& j, const std::string& name) {
  try {
    if (!j.contains(name)) {
      return std::nullopt;
    }
    return j[name].get<T>();
  } catch (std::exception& e) {
    LG_ERROR() << e.what();
    return std::nullopt;
  }
}

template <typename T>
T GetValueFromJsonWithDefault(const json& j, const std::string& name,
                              T default_value) {
  try {
    if (!j.contains(name)) {
      return default_value;
    }
    return j[name].get<T>();
  } catch (std::exception& e) {
    LG_ERROR() << e.what();
    return default_value;
  }
}
}  // namespace

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
      server_config.enabled =
          GetValueFromJson<bool>(server, "enabled").value_or(true);
      server_config.type = GetValueFromJson<std::string>(server, "type")
                               .value_or(std::string{kServerKindStdio});
      std::vector<std::string> args;
      server_config.args =
          GetValueFromJsonWithDefault<std::vector<std::string>>(server,
                                                                "command", {});
      if (server.contains("ssh")) {
        auto ssh = server["ssh"];
        SSHLogin login;
        login.hostname = GetValueFromJson<std::string>(ssh, "hostname")
                             .value_or(std::string{"127.0.0.1"});
        login.ssh_program = GetValueFromJson<std::string>(ssh, "ssh_program")
                                .value_or(std::string{});
        login.ssh_key =
            GetValueFromJson<std::string>(ssh, "key").value_or(std::string{});
        login.user =
            GetValueFromJson<std::string>(ssh, "user").value_or(std::string{});
        login.port = GetValueFromJson<int>(ssh, "port").value_or(22);
        server_config.ssh_login = std::move(login);
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