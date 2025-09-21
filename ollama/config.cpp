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
    if (parsed_data.contains("mcp_servers")) {
      // MCP servers
      auto mcp_servers = parsed_data["mcp_servers"];
      for (const auto& [name, server] : mcp_servers.items()) {
        MCPServerConfig server_config;
        server_config.name = name;
        server_config.enabled =
            GetValueFromJson<bool>(server, "enabled").value_or(true);
        server_config.type = GetValueFromJson<std::string>(server, "type")
                                 .value_or(std::string{kServerKindStdio});
        std::vector<std::string> args;
        server_config.args =
            GetValueFromJsonWithDefault<std::vector<std::string>>(
                server, "command", {});
        if (server.contains("ssh")) {
          auto ssh = server["ssh"];
          SSHLogin login;
          login.hostname = GetValueFromJson<std::string>(ssh, "hostname")
                               .value_or(std::string{"127.0.0.1"});
          login.ssh_program = GetValueFromJson<std::string>(ssh, "ssh_program")
                                  .value_or(std::string{});
          login.ssh_key =
              GetValueFromJson<std::string>(ssh, "key").value_or(std::string{});
          login.user = GetValueFromJson<std::string>(ssh, "user")
                           .value_or(std::string{});
          login.port = GetValueFromJson<int>(ssh, "port").value_or(22);
          server_config.ssh_login = std::move(login);
        }
        config.m_servers.push_back(std::move(server_config));
      }
    }

    if (parsed_data.contains("endpoints")) {
      OLOG(LogLevel::kDebug) << "Parsing endpoints...";
      auto endpoints = parsed_data["endpoints"];
      for (const auto& [endpoint_url, endpoint_json] : endpoints.items()) {
        config.endpoints_.push_back(std::make_shared<Endpoint>());
        auto endpoint = config.endpoints_.back();
        endpoint->url_ = endpoint_url;
        if (endpoint_json.contains("http_headers")) {
          auto http_headers = endpoint_json["http_headers"];
          for (const auto& [header_name, value] : http_headers.items()) {
            endpoint->headers_.insert({header_name, value.get<std::string>()});
          }
        }
        if (endpoint_json.contains("type") &&
            endpoint_json["type"].is_string()) {
          endpoint->type_ = endpoint_json["type"].get<std::string>();
        }
        if (endpoint_json.contains("active") &&
            endpoint_json["active"].is_boolean()) {
          endpoint->active_ = endpoint_json["active"].get<bool>();
        }
      }
    }

    // Always ensure that we have at least 1 endpoint.
    if (config.endpoints_.empty()) {
      config.endpoints_.push_back(std::make_shared<Endpoint>());
      auto endpoint = config.endpoints_.back();
      endpoint->active_ = true;
      endpoint->url_ = "http://127.0.0.1:11434";
      endpoint->headers_.insert({"Host", "127.0.0.1"});
      endpoint->type_ = "ollama";
    }

    // Make sure we have exactly 1 active endpoint.
    bool found_active{false};
    for (auto endpoint : config.endpoints_) {
      if (!found_active) {
        if (endpoint->active_) {
          found_active = true;
        }
      } else {
        // we already have an active one.
        endpoint->active_ = false;
      }
    }

    if (!found_active) {
      // Make the first endpoint active.
      config.endpoints_.front()->active_ = true;
    }

    // print the endpoints.
    for (auto endpoint : config.endpoints_) {
      OLOG(LogLevel::kInfo) << *endpoint;
    }

    // Global setup
    config.m_history_size =
        GetValueFromJsonWithDefault<size_t>(parsed_data, "history_size", 50);

    auto log_level = GetValueFromJson<std::string>(parsed_data, "log_level");
    if (log_level.has_value()) {
      config.m_logLevel = Logger::FromString(log_level.value());
    }
    OLOG(OLogLevel::kInfo) << "Successfully loaded " << config.m_servers.size()
                           << " configurations";

    // Per model configuration
    if (parsed_data.contains("models")) {
      auto models = parsed_data["models"];
      for (const auto& [model_name, j] : models.items()) {
        ModelOptions mo{.name = model_name};
        if (j.contains("options")) {
          mo.options = j["options"];
        }
        if (j.contains("think") && j["think"].is_boolean()) {
          mo.think = j["think"];
        }
        if (j.contains("hidethinking") && j["hidethinking"].is_boolean()) {
          mo.hidethinking = j["hidethinking"];
        }
        if (j.contains("think_start_tag")) {
          mo.think_start_tag = j["think_start_tag"].get<std::string>();
        }
        if (j.contains("think_end_tag")) {
          mo.think_end_tag = j["think_end_tag"].get<std::string>();
        }
        config.m_model_options_map.insert({mo.name, mo});
        OLOG(LogLevel::kInfo) << mo;
      }

      if (config.m_model_options_map.count("default") == 0) {
        // No default model options, add one.
        config.m_model_options_map.insert(
            {"default", CreaetDefaultModelOptions()});
      }
    }
    return config;
  } catch (std::exception& e) {
    OLOG(OLogLevel::kError) << "Failed to parse configuration JSON: " << content
                            << ". " << e.what();
    return std::nullopt;
  }
}

ModelOptions Config::CreaetDefaultModelOptions() {
  using namespace nlohmann::literals;
  ModelOptions mo;
  mo.name = "default";
  mo.options = R"(
  {
    "num_ctx": 32768,
    "temperature": 0
  }
)"_json;
  return mo;
}
}  // namespace ollama
