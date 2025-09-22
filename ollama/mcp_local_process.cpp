#include "mcp_local_process.hpp"

#include "ollama/function.hpp"

namespace ollama {

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

MCPStdioClient::MCPStdioClient(const std::vector<std::string>& args,
                               std::optional<ollama::json> env)
    : m_args(args), m_env(std::move(env)) {}

MCPStdioClient::MCPStdioClient(const SSHLogin& ssh_login,
                               const std::vector<std::string>& args,
                               std::optional<ollama::json> env)
    : m_args(args), m_ssh_login(ssh_login), m_env(std::move(env)) {}

bool MCPStdioClient::Initialise() {
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
                   : ollama::json::object();

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

FunctionResult MCPStdioClient::Call(const mcp::tool& t,
                                    const json& args) const {
  auto result = m_client->call_tool(t.name, args);
  FunctionResult call_result{
      .isError = result["isError"].get<bool>(),
      .text = result["content"][0]["text"].get<std::string>()};
  return call_result;
}

std::vector<std::shared_ptr<FunctionBase>> MCPStdioClient::GetFunctions()
    const {
  std::vector<std::shared_ptr<FunctionBase>> result;
  result.reserve(m_tools.size());
  for (auto t : m_tools) {
    std::shared_ptr<FunctionBase> f = std::make_shared<ExternalFunction>(
        const_cast<MCPStdioClient*>(this), std::move(t));
    result.push_back(std::move(f));
  }
  return result;
}
}  // namespace ollama
