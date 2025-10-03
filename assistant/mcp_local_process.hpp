#pragma once

#include <string>
#include <vector>

#include "assistant/cpp-mcp/mcp_stdio_client.h"
#include "assistant/function_base.hpp"

namespace assistant {
class ExternalFunction;

struct SSHLogin {
  std::string ssh_program{"ssh"};
  std::string ssh_key;
  std::string user;
  std::string hostname{"127.0.0.1"};
  int port{22};
};

class MCPStdioClient {
 public:
  MCPStdioClient(const std::vector<std::string>& args,
                 std::optional<assistant::json> env = {});
  MCPStdioClient(const SSHLogin& ssh_login,
                 const std::vector<std::string>& args,
                 std::optional<assistant::json> env = {});
  ~MCPStdioClient() = default;

  bool Initialise();
  inline bool IsRemote() const { return m_ssh_login.has_value(); }
  inline const std::vector<mcp::tool>& GetTools() const { return m_tools; }
  FunctionResult Call(const mcp::tool& t, const json& args) const;
  std::vector<std::shared_ptr<FunctionBase>> GetFunctions() const;

 private:
  std::vector<std::string> m_args;
  std::vector<mcp::tool> m_tools;
  std::unique_ptr<mcp::stdio_client> m_client;
  std::optional<SSHLogin> m_ssh_login;
  std::optional<assistant::json> m_env;
};
}  // namespace assistant
