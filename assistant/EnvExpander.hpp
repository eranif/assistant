#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "assistant/common/json.hpp"

namespace assistant {

using json = nlohmann::ordered_json;
using EnvMap = std::unordered_map<std::string, std::string>;

class EnvExpander {
 public:
  /**
   * @brief Traverse input json and expand any string type.
   *
   * Recursively traverses the JSON structure and expands environment variables
   * in all string values. Environment variables can be specified in the format
   * ${VAR_NAME} or $VAR_NAME.
   *
   * @param input_json The JSON object to expand.
   * @param map Optional map of environment variables. If not provided,
   *            environment variables are read from the system.
   * @return An expanded version of the input JSON.
   */
  json Expand(json input_json, std::optional<EnvMap> map = std::nullopt) const;

  /**
   * @brief Expand environment variables in a string.
   *
   * Expands environment variables in the format ${VAR_NAME} or $VAR_NAME
   * with their corresponding values. If a variable is not found in the map
   * or system environment, it is left unchanged.
   *
   * @param str The string containing environment variables to expand.
   * @param map Optional map of environment variables. If not provided,
   *            environment variables are read from the system.
   * @return The expanded string with environment variables replaced.
   */
  std::string Expand(const std::string& str,
                     std::optional<EnvMap> map = std::nullopt) const;

 private:
  /**
   * @brief Build an environment variable map from the system environment.
   *
   * @return A map containing all environment variables from the system.
   */
  EnvMap BuildEnvMap() const;

  /**
   * @brief Expand a single environment variable reference.
   *
   * @param str The input string.
   * @param pos The starting position of the variable reference.
   * @param env_map The environment variable map to use for lookup.
   * @param expanded_str The output string to append expanded content to.
   * @return The position in the input string after the variable reference.
   */
  size_t ExpandVariable(const std::string& str, size_t pos,
                        const EnvMap& env_map, std::string& expanded_str) const;
};

}  // namespace assistant
