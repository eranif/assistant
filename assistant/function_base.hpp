#pragma once

#include <string>

#include "assistant/assistantlib.hpp"
#include "assistant/attributes.hpp"
#include "assistant/common.hpp"
#include "assistant/logger.hpp"

namespace assistant {

class MCPClient;
class Config;

template <typename ArgType>
std::optional<ArgType> GetFunctionArg(const assistant::json& args,
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

#define ASSIGN_FUNC_ARG_OR_RETURN(var, expr)                                \
  if (!expr.has_value()) {                                                  \
    return assistant::FunctionResult{.isError = true,                       \
                                     .text = "Missing mandatory argument"}; \
  }                                                                         \
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

struct FunctionResult {
  bool isError{false};
  std::string text;
};

inline std::ostream& operator<<(std::ostream& os,
                                const FunctionResult& result) {
  os << "{ isError = " << result.isError << ", text = '" << result.text
     << "' }";
  return os;
}

using json = nlohmann::ordered_json;
class FunctionBase {
 public:
  FunctionBase(const std::string& name, const std::string& desc)
      : m_name(name), m_desc(desc) {}
  virtual ~FunctionBase() = default;

  json ToJSON(EndpointKind kind) const {
    switch (kind) {
      case EndpointKind::ollama:
        return ToOllamaJson();
      case EndpointKind::openai:
        return ToOpenAIJson();
      case EndpointKind::anthropic:
        return ToClaudeJSON();
    }
    return json({});
  }

  virtual FunctionResult Call(const json& params) const = 0;
  inline const std::string& GetName() const { return m_name; }
  inline const std::string& GetDesc() const { return m_desc; }
  inline bool IsEnabled() const { return m_enabled; }
  inline void SetEnabled(bool b) { m_enabled = b; }

 private:
  json ToOllamaJson() const {
    json j;
    j["type"] = "function";
    j["function"]["name"] = m_name;
    j["function"]["description"] = m_desc;
    j["function"]["parameters"]["type"] = "object";

    std::vector<std::string> required;
    j["function"]["parameters"]["properties"] =
        json({});  // Must always include the "properties" field, even if empty.
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

  json ToOpenAIJson() const {
    auto j = ToOllamaJson();
    j["function"]["strict"] = true;
    j["function"]["parameters"]["additionalProperties"] = false;
    return j;
  }

  json ToClaudeJSON() const {
    json j;
    j["name"] = m_name;
    j["description"] = m_desc;
    j["input_schema"]["type"] = "object";

    std::vector<std::string> required;
    for (const auto& param : m_params) {
      j["input_schema"]["properties"][param.GetName()] = param.ToJSON();
      if (param.IsRequired()) {
        required.push_back(param.GetName());
      }
    }
    j["input_schema"]["required"] = required;
    return j;
  }

 protected:
  std::string m_name;
  std::string m_desc;
  std::vector<Param> m_params;
  std::atomic_bool m_enabled{true};
  friend class FunctionBuilder;
};

struct FunctionCall {
  std::string name;
  json args;
  /// Optional server side invocation ID.
  std::optional<std::string> invocation_id;
};

class FunctionTable {
 public:
  /**
   * @brief Converts the internal state to a JSON representation containing only
   * enabled functions.
   *
   * This method acquires a lock on the internal mutex and iterates through all
   * functions, collecting only those that are enabled into a JSON array.
   *
   * @param kind The endpoint kind to filter or identify the functions
   * @return json A JSON object containing the enabled functions
   */
  json ToJSON(EndpointKind kind, CachePolicy cache_policy) const
      FUNCTION_LOCKS(m_mutex) {
    std::scoped_lock lk{m_mutex};
    std::vector<json> v;
    for (const auto& [_, f] : m_functions) {
      // Only collect enabled functions.
      if (!f->IsEnabled()) {
        continue;
      }

      v.push_back(f->ToJSON(kind));
    }

    if (!v.empty() && cache_policy == CachePolicy::kStatic &&
        kind == assistant::EndpointKind::anthropic) {
      auto& last_tool = v.back();
      last_tool["cache_control"] = {{"type", "ephemeral"}};
    }
    json j = v;
    return j;
  }

  /**
   * @brief Adds a function to the function registry.
   *
   * This method registers a function by inserting it into the internal function
   * map using the function's name as the key. If a function with the same name
   * already exists, a warning is logged and the duplicate is not added.
   *
   * @param f A shared pointer to the FunctionBase object to be added to the
   * registry.
   *
   * @note This method is thread-safe and uses a scoped lock to protect
   * concurrent access.
   * @warning If a duplicate function name is detected, a warning will be
   * logged.
   */
  void Add(std::shared_ptr<FunctionBase> f) FUNCTION_LOCKS(m_mutex) {
    std::scoped_lock lk{m_mutex};
    if (!m_functions.insert({f->GetName(), f}).second) {
      OLOG(OLogLevel::kWarning) << "Duplicate function found: " << f->GetName();
    }
  }

  void AddMCPServer(std::shared_ptr<MCPClient> client) FUNCTION_LOCKS(m_mutex);

  FunctionResult Call(const FunctionCall& func_call) const
      FUNCTION_LOCKS(m_mutex) {
    try {
      std::scoped_lock lk{m_mutex};
      auto iter = m_functions.find(func_call.name);
      if (iter == m_functions.end()) {
        std::stringstream ss;
        ss << "could not find tool: '" << func_call.name << "'";
        FunctionResult result{.isError = true, .text = ss.str()};
        return result;
      }
      return iter->second->Call(func_call.args);

    } catch (std::exception& e) {
      FunctionResult result{.isError = true, .text = e.what()};
      return result;
    }
  }

  void Clear() FUNCTION_LOCKS(m_mutex) {
    std::scoped_lock lk{m_mutex};
    m_functions.clear();
    m_clients.clear();
  }

  void ReloadMCPServers(const Config* config) FUNCTION_LOCKS(m_mutex);
  void Merge(const FunctionTable& other) FUNCTION_LOCKS(m_mutex);

  /**
   * @brief Enables or disables all registered functions in a thread-safe
   * manner.
   *
   * This method iterates through all functions stored in m_functions and sets
   * their enabled state to the specified value. The operation is protected by a
   * mutex lock to ensure thread safety.
   *
   * @param b true to enable all functions, false to disable them
   */
  void EnableAll(bool b) FUNCTION_LOCKS(m_mutex) {
    std::scoped_lock lk{m_mutex};
    for (auto& [name, func] : m_functions) {
      func->SetEnabled(b);
    }
  }

  /**
   * @brief Enables or disables a function by name.
   *
   * This method attempts to find a function with the specified name and modify
   * its enabled state. The operation is thread-safe through mutex protection.
   *
   * @param name The name of the function to enable or disable
   * @param b True to enable the function, false to disable it
   * @return true if the function was found and its state was modified, false if
   * the function does not exist
   */
  inline bool EnableFunction(const std::string& name, bool b)
      FUNCTION_LOCKS(m_mutex) {
    std::scoped_lock lk{m_mutex};
    auto iter = m_functions.find(name);
    if (iter == m_functions.end()) {
      return false;
    }
    iter->second->SetEnabled(b);
    return true;
  }

  /**
   * @brief Returns the count of enabled functions.
   *
   * This method iterates through all functions in the collection and counts
   * only those that are currently enabled. Thread-safe operation is ensured
   * by acquiring a scoped lock on the mutex.
   *
   * @return The number of enabled functions in the collection.
   */
  inline size_t GetFunctionsCount() const FUNCTION_LOCKS(m_mutex) {
    std::scoped_lock lk{m_mutex};
    size_t count{0};
    for (auto& [name, func] : m_functions) {
      if (func->IsEnabled()) {
        count++;
      }
    }
    return count;
  }

  /**
   * @brief synonym to `GetFunctionsCount() == 0`
   */
  inline bool IsEmpty() const { return GetFunctionsCount() == 0; }

 private:
  void AddMCPServerInternal(std::shared_ptr<MCPClient> client)
      CALLER_MUST_LOCK(m_mutex);

  mutable std::mutex m_mutex;
  std::map<std::string, std::shared_ptr<FunctionBase>> m_functions
      GUARDED_BY(m_mutex);
  std::vector<std::shared_ptr<MCPClient>> m_clients GUARDED_BY(m_mutex);
  friend std::ostream& operator<<(std::ostream& os, const FunctionTable& table);
};

inline std::ostream& operator<<(std::ostream& os, const FunctionTable& table) {
  std::scoped_lock lk{table.m_mutex};
  for (const auto& func : table.m_functions) {
    os << "‣ " << "\"" << func.first << "\": " << func.second->GetDesc()
       << std::endl;
  }
  return os;
}

}  // namespace assistant
