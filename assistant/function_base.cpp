#include "assistant/function_base.hpp"

#include "assistant/config.hpp"
#include "assistant/function.hpp"
#include "assistant/mcp.hpp"

namespace assistant {
void FunctionTable::AddMCPServer(std::shared_ptr<MCPClient> client) {
  std::scoped_lock lk{m_mutex};
  AddMCPServerInternal(client);
}

void FunctionTable::AddMCPServerInternal(std::shared_ptr<MCPClient> client) {
  m_clients.push_back(client);
  auto functions = client->GetFunctions();
  for (auto func : functions) {
    if (!m_functions.insert({func->GetName(), func}).second) {
      OLOG(OLogLevel::kWarning)
          << "Duplicate function found: " << func->GetName();
    }
  }
}

void FunctionTable::ReloadMCPServers(const Config* config) {
  if (config == nullptr) {
    return;
  }

  std::scoped_lock lk{m_mutex};
  // Clear all current MCP servers and their functions.
  std::vector<std::string> names;
  for (const auto& [funcname, func] : m_functions) {
    if (dynamic_cast<ExternalFunction*>(func.get()) != nullptr) {
      names.push_back(funcname);
    }
  }

  for (const auto& funcname : names) {
    m_functions.erase(funcname);
    OLOG(LogLevel::kInfo) << "Deleting MCP server function: " << funcname;
  }
  m_clients.clear();

  for (const auto& s : config->GetServers()) {
    if (!s.enabled) {
      continue;
    }
    OLOG(LogLevel::kInfo) << "Starting MCP server: " << s.name;
    std::shared_ptr<MCPClient> client{nullptr};
    if (s.IsStdio()) {
      const auto& params = s.stdio_params.value();
      if (params.IsRemote()) {
        client = std::make_shared<MCPClient>(params.ssh_login.value(),
                                             params.args, params.env);
      } else {
        client = std::make_shared<MCPClient>(params.args, params.env);
      }
    } else if (s.IsSse()) {
      const auto& params = s.sse_params.value();
      std::vector<std::pair<std::string, std::string>> http_headers;
      if (params.headers.has_value()) {
        const auto& headers = params.headers.value();
        http_headers.reserve(headers.size());
        for (const auto& [name, value] : headers.items()) {
          http_headers.emplace_back(name, value.get<std::string>());
        }
      }
      client = std::make_shared<MCPClient>(params.baseurl, params.endpoint,
                                           params.auth_token.value_or(""),
                                           http_headers);
    }

    if (client && client->Initialise()) {
      AddMCPServerInternal(client);
    } else {
      OLOG(LogLevel::kWarning)
          << "Failed to initialise client for MCP server: " << s.name;
    }
  }
}

void FunctionTable::Merge(const FunctionTable& other) {
  // Lock both tables.
  std::lock_guard lk1{m_mutex};
  std::lock_guard lk2{other.m_mutex};

  for (auto [name, f] : other.m_functions) {
    if (m_functions.contains(name)) {
      continue;
    }
    m_functions.insert({name, f});
  }
}

}  // namespace assistant
