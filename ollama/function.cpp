#include "ollama/function.hpp"

#include "ollama/mcp_local_process.hpp"

namespace ollama {
ExternalFunction::ExternalFunction(ollama::MCPStdioClient* client, mcp::tool t)
    : FunctionBase(t.name, t.description),
      m_client(client),
      m_tool(std::move(t)) {
  try {
    auto properties = m_tool.parameters_schema["properties"];
    auto required = m_tool.parameters_schema["required"];

    std::unordered_set<std::string> required_param = {required.begin(),
                                                      required.end()};
    for (const auto& [name, obj] : properties.items()) {
      Param p(name, obj["description"], obj["type"],
              required_param.count(name));
      m_params.push_back(std::move(p));
    }
  } catch (std::exception& e) {
    LOG_WARNING() << "Failed to build external function from tool. "
                  << e.what();
  }
}

std::string ExternalFunction::Call(const json& args) const {
  return m_client->Call(m_tool, args);
}
}  // namespace ollama
