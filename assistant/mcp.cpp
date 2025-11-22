#include "mcp.hpp"

#include "assistant/cpp-mcp/mcp_client.h"
#include "assistant/cpp-mcp/mcp_sse_client.h"
#include "assistant/cpp-mcp/mcp_stdio_client.h"
#include "assistant/function.hpp"

namespace assistant {

namespace {
void WrapWithDoubleQuotes(std::string& s) {
  if (!s.empty()                             // not empty
      && (s.find(" ") != std::string::npos)  // contains space
      && s[0] != '"'  // does not already wrapped with double quotes
  ) {
    s.insert(s.begin(), '"');
    s.push_back('"');
  }
}

void EscapeDoubleQuotes(std::string& str) {
  if (str.empty()) {
    return;
  }

  std::string result;
  result.reserve(str.size() * 2);
  for (auto c : str) {
    if (c == '"') {
      result.append("\\\"");
    } else {
      result.push_back(c);
    }
  }
  str.swap(result);
}
}  // namespace

MCPClient::MCPClient(const std::vector<std::string>& args,
                     std::optional<assistant::json> env)
    : m_args(args), m_env(std::move(env)) {}

MCPClient::MCPClient(
    const std::string& base_url, const std::string& sse_endpoint,
    const std::string& auth_token,
    const std::vector<std::pair<std::string, std::string>>& headers)
    : m_base_url{base_url},
      m_sse_endpoint{sse_endpoint},
      m_auth_token{auth_token},
      m_headers{headers},
      m_is_sse{true} {}

MCPClient::MCPClient(const SSHLogin& ssh_login,
                     const std::vector<std::string>& args,
                     std::optional<assistant::json> env)
    : m_args(args), m_ssh_login(ssh_login), m_env(std::move(env)) {}

bool MCPClient::InitialiseSSE() {
  try {
    auto c = new mcp::sse_client(m_base_url, m_sse_endpoint);
    if (!m_auth_token.empty()) {
      c->set_auth_token(m_auth_token);
    }
    // For now set an **empty** json array for capabilities to avoid sending a
    // null one.
    c->set_capabilities(json({}));
    c->initialize("assistant", "1.0");
    for (const auto& [k, v] : m_headers) {
      c->set_header(k, v);
    }
    c->ping();
    m_tools = c->get_tools();
    m_client.reset(c);
    return true;
  } catch (std::exception& e) {
    OLOG(LogLevel::kWarning) << e.what();
    return false;
  }
}

bool MCPClient::InitialiseStdio() {
  try {
    std::stringstream ss;
    for (size_t i = 0; i < m_args.size(); ++i) {
      if (i != 0) {
        ss << " ";
      }
      WrapWithDoubleQuotes(m_args[i]);
      ss << m_args[i];
    }
    std::string command = ss.str();

    ss = {};
    if (IsRemote()) {
      SSHLogin ssh_login = m_ssh_login.value();

      WrapWithDoubleQuotes(ssh_login.ssh_program);
      WrapWithDoubleQuotes(ssh_login.user);
      WrapWithDoubleQuotes(ssh_login.ssh_key);
      ss << ssh_login.ssh_program;
      if (!ssh_login.ssh_key.empty()) {
        ss << " -i " << ssh_login.ssh_key;
      }

      if (!ssh_login.user.empty()) {
        ss << " -l " << ssh_login.user;
      }

      ss << " -o ServerAliveInterval=30";  // Keep alive connection
      ss << " -p " << ssh_login.port << " " << ssh_login.hostname << " ";

      // escape the command if needed
      EscapeDoubleQuotes(command);
      ss << '"' << command << '"';
      command = ss.str();
    }

    OLOG(LogLevel::kInfo) << "Starting MCP server: " << command;
    // Pass the environment variables
    auto env = (m_env.has_value() && m_env.value().is_object())
                   ? m_env.value()
                   : assistant::json::object();

    m_client.reset(new mcp::stdio_client(command, env));
    m_client->initialize("assistant", "1.0");
    m_client->ping();
    m_tools = m_client->get_tools();
    OLOG(LogLevel::kInfo) << "Success!";
    return true;
  } catch (std::exception& e) {
    OLOG(LogLevel::kWarning) << e.what();
    return false;
  }
}

bool MCPClient::Initialise() {
  if (m_is_sse) {
    return InitialiseSSE();
  } else {
    return InitialiseStdio();
  }
}

FunctionResult MCPClient::Call(const mcp::tool& t, const json& args) const {
  auto result = m_client->call_tool(t.name, args);
  FunctionResult call_result{
      .isError = result["isError"].get<bool>(),
      .text = result["content"][0]["text"].get<std::string>()};
  return call_result;
}

std::vector<std::shared_ptr<FunctionBase>> MCPClient::GetFunctions() const {
  std::vector<std::shared_ptr<FunctionBase>> result;
  result.reserve(m_tools.size());
  for (auto t : m_tools) {
    std::shared_ptr<FunctionBase> f = std::make_shared<ExternalFunction>(
        const_cast<MCPClient*>(this), std::move(t));
    result.push_back(std::move(f));
  }
  return result;
}
}  // namespace assistant
