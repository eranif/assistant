#include "ollama/function_base.hpp"

#include "ollama/mcp_local_process.hpp"

namespace ollama {
void FunctionTable::AddMCPServer(std::shared_ptr<MCPStdioClient> client) {
  m_clients.push_back(client);
  auto functions = client->GetFunctions();
  for (auto func : functions) {
    Add(func);
  }
}

}  // namespace ollama
