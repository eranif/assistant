#include "assistant/EnvExpander.hpp"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
extern char** environ;
#endif

namespace assistant {

json EnvExpander::Expand(json input_json, std::optional<EnvMap> map) const {
  // Build environment map if not provided
  EnvMap env_map = map.has_value() ? map.value() : BuildEnvMap();

  // Recursively traverse and expand the JSON
  if (input_json.is_string()) {
    // Expand string values
    std::string str = input_json.get<std::string>();
    input_json = Expand(str, env_map);
  } else if (input_json.is_object()) {
    // Recursively expand object members
    for (auto& [key, value] : input_json.items()) {
      value = Expand(value, env_map);
    }
  } else if (input_json.is_array()) {
    // Recursively expand array elements
    for (auto& element : input_json) {
      element = Expand(element, env_map);
    }
  }
  // For other types (numbers, booleans, null), return as-is

  return input_json;
}

std::string EnvExpander::Expand(const std::string& str,
                                std::optional<EnvMap> map) const {
  // Build environment map if not provided
  EnvMap env_map = map.has_value() ? map.value() : BuildEnvMap();

  std::string result;
  result.reserve(str.size());

  size_t pos = 0;
  while (pos < str.size()) {
    // Look for '$' character
    if (str[pos] == '$') {
      pos = ExpandVariable(str, pos, env_map, result);
    } else {
      result += str[pos];
      ++pos;
    }
  }

  return result;
}

EnvMap EnvExpander::BuildEnvMap() const {
  EnvMap env_map;

#ifdef _WIN32
  // Windows implementation
  LPCH env_strings = GetEnvironmentStrings();
  if (env_strings != nullptr) {
    LPCH current = env_strings;
    while (*current != '\0') {
      std::string env_entry(current);
      size_t eq_pos = env_entry.find('=');
      if (eq_pos != std::string::npos && eq_pos > 0) {
        std::string key = env_entry.substr(0, eq_pos);
        std::string value = env_entry.substr(eq_pos + 1);
        env_map[key] = value;
      }
      current += env_entry.size() + 1;
    }
    FreeEnvironmentStrings(env_strings);
  }
#else
  // Unix-like systems
  if (environ != nullptr) {
    for (char** env = environ; *env != nullptr; ++env) {
      std::string env_entry(*env);
      size_t eq_pos = env_entry.find('=');
      if (eq_pos != std::string::npos) {
        std::string key = env_entry.substr(0, eq_pos);
        std::string value = env_entry.substr(eq_pos + 1);
        env_map[key] = value;
      }
    }
  }
#endif

  return env_map;
}

size_t EnvExpander::ExpandVariable(const std::string& str, size_t pos,
                                   const EnvMap& env_map,
                                   std::string& expanded_str) const {
  // pos points to '$'
  if (pos + 1 >= str.size()) {
    // '$' at end of string
    expanded_str += '$';
    return pos + 1;
  }

  size_t start_pos = pos + 1;
  std::string var_name;
  bool has_braces = false;

  // Check for ${VAR_NAME} format
  if (str[start_pos] == '{') {
    has_braces = true;
    ++start_pos;
    size_t end_pos = str.find('}', start_pos);
    if (end_pos == std::string::npos) {
      // No closing brace found, treat as literal
      expanded_str += str.substr(pos, 2);  // Add "${"
      return start_pos;
    }
    var_name = str.substr(start_pos, end_pos - start_pos);
    pos = end_pos + 1;
  } else {
    // $VAR_NAME format - extract alphanumeric and underscore characters
    size_t end_pos = start_pos;
    while (end_pos < str.size() &&
           (std::isalnum(static_cast<unsigned char>(str[end_pos])) ||
            str[end_pos] == '_')) {
      ++end_pos;
    }

    if (end_pos == start_pos) {
      // No valid variable name after '$'
      expanded_str += '$';
      return start_pos;
    }

    var_name = str.substr(start_pos, end_pos - start_pos);
    pos = end_pos;
  }

  // Look up the variable in the map
  auto it = env_map.find(var_name);
  if (it != env_map.end()) {
    expanded_str += it->second;
  } else {
    // Variable not found, keep the original text
    if (has_braces) {
      expanded_str += "${" + var_name + "}";
    } else {
      expanded_str += "$" + var_name;
    }
  }

  return pos;
}

}  // namespace assistant
