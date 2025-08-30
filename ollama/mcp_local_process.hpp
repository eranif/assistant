#pragma once

#include <string>
#include <vector>

#include "ollama/cpp-mcp/mcp_stdio_client.h"
#include "ollama/function_base.hpp"

namespace ollama {
class ExternalFunction;

class MCPStdioClient {
 public:
  MCPStdioClient(const std::vector<std::string>& args);
  ~MCPStdioClient() = default;

  bool Initialise();
  inline const std::vector<mcp::tool>& GetTools() const { return m_tools; }
  std::string Call(const mcp::tool& t, const json& args) const;
  std::vector<std::shared_ptr<FunctionBase>> GetFunctions();

 private:
  std::vector<std::string> m_args;
  std::vector<mcp::tool> m_tools;
  std::unique_ptr<mcp::stdio_client> m_client;
};
}  // namespace ollama
