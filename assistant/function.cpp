#include "assistant/function.hpp"

#include "assistant/mcp.hpp"

#include <ranges>

namespace assistant {
ExternalFunction::ExternalFunction(assistant::MCPClient* client, mcp::tool t)
    : FunctionBase(t.name, t.description),
      m_client(client),
      m_tool(std::move(t)) {
  try {
    const auto& properties = m_tool.parameters_schema["properties"];
    const auto& required = m_tool.parameters_schema["required"];
    const auto required_str =
        required | std::ranges::views::transform([](const json& json) {
          return json.get<std::string>();
        });
    const std::unordered_set<std::string> required_param = {
        required_str.begin(), required_str.end()};
    for (const auto& [name, obj] : properties.items()) {
      if (obj.contains("description") && obj.contains("type")) {
        // The spec requires "description" & "type"
        Param p(name, obj["description"].get<std::string>(),
                obj["type"].get<std::string>(), required_param.count(name));
        m_params.push_back(std::move(p));
      } else if (obj.contains("type") && obj.contains("title")) {
        // FastMCP, by default will generate "type" & "title"
        Param p(name, obj["title"].get<std::string>(),
                obj["type"].get<std::string>(), required_param.count(name));
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
