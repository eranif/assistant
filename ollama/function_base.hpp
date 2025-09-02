#pragma once

#include <string>

#include "ollama/logger.hpp"
#include "ollama/ollamalib.hpp"

namespace ollama {

class MCPStdioClient;
template <typename ArgType>
std::optional<ArgType> GetFunctionArg(const ollama::json& args,
                                      const std::string& name) {
  try {
    if (!args.contains(name)) {
      return std::nullopt;
    }
    ArgType arg = args[name];
    return arg;
  } catch (...) {
    return std::nullopt;
  }
}

#define ASSIGN_FUNC_ARG_OR_RETURN(var, expr) \
  if (!expr.has_value()) {                   \
    return "Missing mandatory argument";     \
  }                                          \
  var = expr.value();

class Param {
 public:
  Param(const std::string& name, const std::string& desc,
        const std::string& type, bool required)
      : m_name(name), m_desc(desc), m_type(type), m_required(required) {}

  json ToJSON() const {
    json j;
    j["type"] = m_type;
    j["description"] = m_desc;
    return j;
  }
  inline const std::string& GetName() const { return m_name; }
  inline bool IsRequired() const { return m_required; }

 private:
  std::string m_name;
  std::string m_desc;
  std::string m_type;
  bool m_required{true};
};

class FunctionBase {
 public:
  FunctionBase(const std::string& name, const std::string& desc)
      : m_name(name), m_desc(desc) {}

  json ToJSON() const {
    json j;
    j["type"] = "function";
    j["function"]["name"] = m_name;
    j["function"]["description"] = m_desc;
    j["function"]["parameters"]["type"] = "object";

    std::vector<std::string> required;
    for (const auto& param : m_params) {
      j["function"]["parameters"]["properties"][param.GetName()] =
          param.ToJSON();
      if (param.IsRequired()) {
        required.push_back(param.GetName());
      }
    }
    j["function"]["parameters"]["required"] = required;
    return j;
  }

  virtual std::string Call(const json& params) const = 0;
  inline const std::string& GetName() const { return m_name; }

 protected:
  std::string m_name;
  std::string m_desc;
  std::vector<Param> m_params;
  friend class FunctionBuilder;
};

struct FunctionCall {
  std::string name;
  json args;
};

class FunctionTable {
 public:
  json ToJSON() const {
    std::vector<json> v;
    for (const auto& [_, f] : m_functions) {
      v.push_back(f->ToJSON());
    }
    json j = v;
    return j;
  }

  void Add(std::shared_ptr<FunctionBase> f) {
    if (!m_functions.insert({f->GetName(), f}).second) {
      OLOG(OLogLevel::kWarning) << "Duplicate function found: " << f->GetName();
    }
  }

  void AddMCPServer(std::shared_ptr<MCPStdioClient> client);
  std::string Call(const FunctionCall& func_call) const {
    auto iter = m_functions.find(func_call.name);
    if (iter == m_functions.end()) {
      std::stringstream ss;
      ss << "could not find tool: '" << func_call.name << "'";
      return ss.str();
    }
    return iter->second->Call(func_call.args);
  }

  void Clear() {
    m_functions.clear();
    m_clients.clear();
  }

 private:
  std::map<std::string, std::shared_ptr<FunctionBase>> m_functions;
  std::vector<std::shared_ptr<MCPStdioClient>> m_clients;
};

}  // namespace ollama
