#pragma once

#include <string>
#include <vector>

#include "assistant/cpp-mcp/mcp_client.h"
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

class MCPClient {
 public:
  MCPClient(const std::vector<std::string>& args,
            std::optional<assistant::json> env = {});
  MCPClient(
      const std::string& base_url, const std::string& sse_endpoint = "/sse",
      const std::string& auth_token = {},
      const std::vector<std::pair<std::string, std::string>>& headers = {});
  MCPClient(const SSHLogin& ssh_login, const std::vector<std::string>& args,
            std::optional<assistant::json> env = {});
  ~MCPClient() = default;

  bool Initialise();
  inline bool IsRemote() const { return m_ssh_login.has_value(); }
  inline const std::vector<mcp::tool>& GetTools() const { return m_tools; }
  FunctionResult Call(const mcp::tool& t, const json& args) const;
  std::vector<std::shared_ptr<FunctionBase>> GetFunctions() const;

 private:
  bool InitialiseStdio();
  bool InitialiseSSE();

  std::vector<std::string> m_args;
  std::vector<mcp::tool> m_tools;
  std::unique_ptr<mcp::client> m_client;
  std::optional<SSHLogin> m_ssh_login;
  std::optional<assistant::json> m_env;
  // sse related
  std::string m_base_url;
  std::string m_sse_endpoint{"/sse"};
  std::string m_auth_token;
  std::vector<std::pair<std::string, std::string>> m_headers;
  bool m_is_sse{false};
};
}  // namespace assistant
