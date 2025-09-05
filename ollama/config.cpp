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
    OLOG(OLogLevel::kError) << e.what();
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
    OLOG(OLogLevel::kError) << e.what();
    return default_value;
  }
}
}  // namespace

std::optional<Config> Config::FromFile(const std::string& filepath) {
  try {
    std::ifstream input_file(filepath);
    if (!input_file.is_open()) {
      OLOG(OLogLevel::kError) << "Failed to open file: " << filepath;
      return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input_file.rdbuf();  // Read the entire file stream into the
                                   // string stream
    std::string file_content = buffer.str();  // Extract the
    input_file.close();
    return FromContent(file_content);
  } catch (std::exception& e) {
    OLOG(OLogLevel::kError)
        << "Failed to parse configuration file: " << filepath << ". "
        << e.what();
    return std::nullopt;
  }
}

std::optional<Config> Config::FromContent(const std::string& content) {
  try {
    Config config;
    json parsed_data = json::parse(content);
    auto servers = parsed_data["servers"];
    for (const auto& server : servers) {
      MCPServerConfig server_config;
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
      config.m_servers.push_back(std::move(server_config));
    }

    config.m_url = GetValueFromJsonWithDefault<std::string>(
        parsed_data, "server_url", "http://127.0.0.1:11434");
    config.m_use_gpu =
        GetValueFromJsonWithDefault<bool>(parsed_data, "use_gpu", true);
    config.m_history_size =
        GetValueFromJsonWithDefault<size_t>(parsed_data, "history_size", 20);
    config.m_context_size =
        GetValueFromJsonWithDefault<size_t>(parsed_data, "context_size", 32768);

    OLOG(OLogLevel::kInfo) << "Successfully loaded " << config.m_servers.size()
                           << " configurations";
    return config;
  } catch (std::exception& e) {
    OLOG(OLogLevel::kError) << "Failed to parse configuration JSON: " << content
                            << ". " << e.what();
    return std::nullopt;
  }
}
}  // namespace ollama