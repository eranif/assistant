#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ollama/ollama.hpp"

namespace ollama::tool {

#define MANDATORY_PARAM(name, desc) Param name()

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

  std::string Call(const std::vector<Param>& params) {
    return m_callback(params);
  }
  inline const std::string& GetName() const { return m_name; }

 private:
  std::string m_name;
  std::string m_desc;
  std::vector<Param> m_params;
  std::function<std::string(const std::vector<Param>&)> m_callback;
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
      std::function<std::string(const std::vector<Param>&)> func) {
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
  std::function<std::string(const std::vector<Param>&)> m_func;
  std::vector<Param> m_params;
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

 private:
  std::map<std::string, Function> m_functions;
};
}  // namespace ollama::tool
