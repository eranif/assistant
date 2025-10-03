#include "assistant/function_base.hpp"

#include "assistant/config.hpp"
#include "assistant/function.hpp"
#include "assistant/mcp_local_process.hpp"

namespace assistant {
void FunctionTable::AddMCPServer(std::shared_ptr<MCPStdioClient> client) {
  std::lock_guard lk{m_mutex};
  m_clients.push_back(client);
  auto functions = client->GetFunctions();
  for (auto func : functions) {
    Add(func);
  }
}

void FunctionTable::ReloadMCPServers(const Config* config) {
  if (config == nullptr) {
    return;
  }

  std::lock_guard lk{m_mutex};
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
    if (s.type == assistant::kServerKindStdio) {
      std::shared_ptr<MCPStdioClient> client{nullptr};
      if (s.IsRemote()) {
        client = std::make_shared<MCPStdioClient>(s.ssh_login.value(), s.args,
                                                  s.env);
      } else {
        client = std::make_shared<MCPStdioClient>(s.args, s.env);
      }
      if (client->Initialise()) {
        AddMCPServer(client);
      } else {
        OLOG(LogLevel::kWarning)
            << "Failed to initialise client for MCP server: " << s.name;
      }
    } else {
      OLOG(OLogLevel::kWarning)
          << "Server of type: " << s.type << " are not supported yet";
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
