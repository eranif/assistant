#include "mcp_local_process.hpp"

#include "ollama/function.hpp"
#include "ollama/ollama.hpp"

namespace ollama {
MCPStdioClient::MCPStdioClient(const std::vector<std::string>& args)
    : m_args(args) {}

bool MCPStdioClient::Initialise() {
  try {
    std::stringstream ss;
    for (size_t i = 0; i < m_args.size(); ++i) {
      if (i != 0) {
        ss << " ";
      }
      if (m_args[i].find(" ") != std::string::npos) {
        ss << "\"" << m_args[i] << "\"";
      } else {
        ss << m_args[i];
      }
    }

    m_client.reset(new mcp::stdio_client(ss.str()));
    m_client->initialize("assistant", "1.0");
    m_client->ping();
    m_tools = m_client->get_tools();
    return true;
  } catch (std::exception& e) {
    return false;
  }
}

std::string MCPStdioClient::Call(const mcp::tool& t, const json& args) const {
  auto result = m_client->call_tool(t.name, args);
  return result["content"][0]["text"];
}

std::vector<std::shared_ptr<FunctionBase>> MCPStdioClient::GetFunctions() {
  std::vector<std::shared_ptr<FunctionBase>> result;
  result.reserve(m_tools.size());
  for (auto t : m_tools) {
    std::shared_ptr<FunctionBase> f =
        std::make_shared<ExternalFunction>(this, t);
    result.push_back(std::move(f));
  }
  return result;
}
}  // namespace ollama
