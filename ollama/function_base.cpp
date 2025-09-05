#include "ollama/function_base.hpp"

#include "ollama/config.hpp"
#include "ollama/function.hpp"
#include "ollama/mcp_local_process.hpp"

namespace ollama {
void FunctionTable::AddMCPServer(std::shared_ptr<MCPStdioClient> client) {
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
    if (s.type == ollama::kServerKindStdio) {
      std::shared_ptr<MCPStdioClient> client{nullptr};
      if (s.IsRemote()) {
        client = std::make_shared<MCPStdioClient>(s.ssh_login.value(), s.args);
      } else {
        client = std::make_shared<MCPStdioClient>(s.args);
      }
      if (client->Initialise()) {
        AddMCPServer(client);
      }
    } else {
      OLOG(OLogLevel::kWarning)
          << "Server of type: " << s.type << " are not supported yet";
    }
  }
}
}  // namespace ollama
