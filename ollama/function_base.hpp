#pragma once

#include <string>

#include "ollama/ollamalib.hpp"

namespace ollama {

#define ASSIGN_FUNC_ARG_OR_RETURN(var, expr) \
  if (!expr.has_value()) {                   \
    return "Invalid argument";               \
  }                                          \
  var = expr.value();

enum class ParamType {
  kString,
  kNumber,
  kBoolean,
};

inline std::string ParamTypeToString(ParamType type) {
  switch (type) {
    case ParamType::kString:
      return "string";
    case ParamType::kNumber:
      return "number";
    default:
    case ParamType::kBoolean:
      return "boolean";
  }
}

class Param {
 public:
  Param(const std::string& name, const std::string& desc, ParamType type,
        bool required)
      : m_name(name), m_desc(desc), m_type(type), m_required(required) {}

  json ToJSON() const {
    json j;
    j["type"] = ParamTypeToString(m_type);
    j["description"] = m_desc;
    return j;
  }
  inline const std::string& GetName() const { return m_name; }
  inline bool IsRequired() const { return m_required; }

 private:
  std::string m_name;
  std::string m_desc;
  ParamType m_type{ParamType::kString};
  bool m_required{true};
};

struct FunctionArgument {
  std::string name;
  json value;
};

struct FunctionArgumentVec {
  std::vector<FunctionArgument> args;
  std::optional<json> GetArg(const std::string& name) const {
    auto where = std::find_if(
        args.begin(), args.end(),
        [name](const FunctionArgument& arg) { return arg.name == name; });
    if (where == args.end()) {
      return std::nullopt;
    }
    return where->value;
  }

  void Add(FunctionArgument arg) { args.push_back(std::move(arg)); }
  size_t GetSize() const { return args.size(); }
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

  virtual std::string Call(const FunctionArgumentVec& params) const = 0;
  inline const std::string& GetName() const { return m_name; }

 protected:
  std::string m_name;
  std::string m_desc;
  std::vector<Param> m_params;
  friend class FunctionBuilder;
};

struct FunctionCall {
  std::string name;
  FunctionArgumentVec args;
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
    m_functions.insert({f->GetName(), f});
  }
  std::string Call(const FunctionCall& func_call) const {
    auto iter = m_functions.find(func_call.name);
    if (iter == m_functions.end()) {
      std::stringstream ss;
      ss << "could not find tool: '" << func_call.name << "'";
      return ss.str();
    }
    return iter->second->Call(func_call.args);
  }

  void Clear() { m_functions.clear(); }

 private:
  std::map<std::string, std::shared_ptr<FunctionBase>> m_functions;
};

}  // namespace ollama
