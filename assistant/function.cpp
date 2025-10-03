#include "assistant/function.hpp"

#include "assistant/mcp_local_process.hpp"

namespace assistant {
ExternalFunction::ExternalFunction(assistant::MCPStdioClient* client, mcp::tool t)
    : FunctionBase(t.name, t.description),
      m_client(client),
      m_tool(std::move(t)) {
  try {
    auto properties = m_tool.parameters_schema["properties"];
    auto required = m_tool.parameters_schema["required"];

    std::unordered_set<std::string> required_param = {required.begin(),
                                                      required.end()};
    for (const auto& [name, obj] : properties.items()) {
      if (obj.contains("description") && obj.contains("type")) {
        // The spec requires "description" & "type"
        Param p(name, obj["description"], obj["type"],
                required_param.count(name));
        m_params.push_back(std::move(p));
      } else if (obj.contains("type") && obj.contains("title")) {
        // FastMCP, by default will generate "type" & "title"
        Param p(name, obj["title"], obj["type"], required_param.count(name));
        m_params.push_back(std::move(p));
      }
    }
  } catch (std::exception& e) {
    OLOG(OLogLevel::kWarning) << "Failed to build external function from tool. "
                  << e.what();
  }
}

FunctionResult ExternalFunction::Call(const json& args) const {
  return m_client->Call(m_tool, args);
}
}  // namespace assistant
