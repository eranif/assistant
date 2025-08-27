#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ollamalib.hpp"

namespace ollama::tool {

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

inline ParamType ParamTypeFromString(std::string str) {
  std::transform(str.begin(), str.end(), str.begin(),
                 [](char c) { return std::tolower(c); });
  if (str == "string") {
    return ParamType::kString;
  } else if (str == "number" || str == "integer") {
    return ParamType::kNumber;
  } else {
    return ParamType::kBoolean;
  }
}

class Param {
 public:
  Param(std::string_view name, std::string_view desc, ParamType type,
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
  std::optional<json> GetArg(std::string_view name) const {
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

class Function {
 public:
  Function(std::string_view name, std::string_view desc)
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

  std::string Call(const FunctionArgumentVec& params) const {
    return m_callback(params);
  }
  inline const std::string& GetName() const { return m_name; }

 private:
  std::string m_name;
  std::string m_desc;
  std::vector<Param> m_params;
  std::function<std::string(const FunctionArgumentVec&)> m_callback;
  friend class FunctionBuilder;
};

class FunctionBuilder {
 public:
  FunctionBuilder(std::string_view name) : m_name(name) {}
  FunctionBuilder& SetDescription(std::string_view desc) {
    m_desc = desc;
    return *this;
  }

  FunctionBuilder& AddParam(Param param) {
    m_params.push_back(std::move(param));
    return *this;
  }

  FunctionBuilder& AddRequiredParam(std::string_view name,
                                    std::string_view desc, ParamType type) {
    m_params.push_back({name, desc, type, true});
    return *this;
  }

  FunctionBuilder& AddOptionalParam(std::string_view name,
                                    std::string_view desc, ParamType type) {
    m_params.push_back({name, desc, type, false});
    return *this;
  }

  FunctionBuilder& AddCallback(
      std::function<std::string(const FunctionArgumentVec&)> func) {
    m_func = std::move(func);
    return *this;
  }

  Function Build() {
    Function f(m_name, m_desc);
    f.m_params = std::move(m_params);
    f.m_callback = std::move(m_func);
    return f;
  }

 private:
  std::string m_name;
  std::string m_desc;
  std::function<std::string(const FunctionArgumentVec&)> m_func;
  std::vector<Param> m_params;
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
      v.push_back(f.ToJSON());
    }
    json j = v;
    return j;
  }

  void Add(Function f) { m_functions.insert({f.GetName(), std::move(f)}); }
  std::string Call(const FunctionCall& func_call) const {
    auto iter = m_functions.find(func_call.name);
    if (iter == m_functions.end()) {
      std::stringstream ss;
      ss << "could not find tool: '" << func_call.name << "'";
      return ss.str();
    }
    return iter->second.Call(func_call.args);
  }

  void Clear() { m_functions.clear(); }

 private:
  std::map<std::string, Function> m_functions;
};

class ResponseParser {
 public:
  static std::optional<std::vector<FunctionCall>> GetTools(
      const ollama::response& resp) {
    try {
      json j = resp.as_json();
      std::vector<FunctionCall> calls;
      std::vector<json> tools = j["message"]["tool_calls"];
      for (json tool : tools) {
        FunctionCall function_call;
        function_call.name = tool["function"]["name"];
        auto items = tool["function"]["arguments"].items();
        for (const auto& [name, value] : items) {
          function_call.args.Add({name, value});
        }
        calls.push_back(std::move(function_call));
      }
      return calls;
    } catch (std::exception& e) {
      return std::nullopt;
    }
  }

  static std::optional<ollama::message> GetMessage(
      const ollama::response& resp) {
    try {
      json j = resp.as_json();
      auto msg = ollama::message(j["message"]["role"], j["message"]["content"]);
      if (j["message"].contains("tool_calls")) {
        msg["tool_calls"] = j["message"]["tool_calls"];
      }
      return msg;
    } catch (std::exception& e) {
      return std::nullopt;
    }
  }

  static std::optional<std::string> GetContent(const ollama::response& resp) {
    try {
      json j = resp.as_json();
      return j["message"]["content"];
    } catch (std::exception& e) {
      return std::nullopt;
    }
  }

  static bool IsDone(const ollama::response& resp) {
    try {
      json j = resp.as_json();
      return j["done"];
    } catch (std::exception&) {
    }
    return false;
  }
};
}  // namespace ollama::tool
