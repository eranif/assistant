#pragma once

#include <string>
#include <vector>

#include "ollama/cpp-mcp/mcp_stdio_client.h"
#include "ollama/function_base.hpp"

namespace ollama {

class MCPStdioClient {
 public:
  MCPStdioClient(const std::vector<std::string>& args);
  ~MCPStdioClient() = default;

  bool Initialise();
  inline const std::vector<mcp::tool>& GetTools() const { return tools_; }
  std::string Call(const mcp::tool& t, const FunctionArgumentVec& params) const;

 private:
  std::vector<std::string> args_;
  std::vector<mcp::tool> tools_;
  std::unique_ptr<mcp::stdio_client> client_;
};
}  // namespace ollama
