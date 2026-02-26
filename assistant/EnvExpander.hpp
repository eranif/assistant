#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "assistant/common/json.hpp"

namespace assistant {

using json = nlohmann::ordered_json;
using EnvMap = std::unordered_map<std::string, std::string>;

/**
 * @brief Result class for environment variable expansion operations.
 *
 * This class encapsulates the result of an expansion operation, including
 * both the expanded value and a success indicator. When success is false,
 * it means at least one environment variable could not be resolved.
 */
class ExpandResult {
 public:
  /**
   * @brief Construct a successful result.
   */
  ExpandResult() : success_(true) {}

  /**
   * @brief Construct a result with explicit success status.
   */
  explicit ExpandResult(bool success) : success_(success) {}

  /**
   * @brief Check if the expansion was successful.
   * @return true if all environment variables were successfully resolved.
   */
  bool IsSuccess() const { return success_; }

  /**
   * @brief Set the success status.
   */
  void SetSuccess(bool success) { success_ = success; }

  /**
   * @brief Get the error message.
   * @return The error message indicating which variable(s) failed to expand.
   */
  const std::string& GetErrorMessage() const { return message_; }

  /**
   * @brief Set the error message.
   */
  void SetErrorMessage(const std::string& message) { message_ = message; }
  void SetErrorMessage(std::string&& message) { message_ = std::move(message); }

  /**
   * @brief Get the result value (for string expansion).
   */
  const std::string& GetString() const { return str_value_; }
  std::string& GetString() { return str_value_; }

  /**
   * @brief Get the result value (for JSON expansion).
   */
  const json& GetJson() const { return json_value_; }
  json& GetJson() { return json_value_; }

 private:
  std::string str_value_;
  json json_value_;
  std::string message_;
  bool success_;
};

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
   * @brief Traverse input json and expand any string type (with result status).
   *
   * Recursively traverses the JSON structure and expands environment variables
   * in all string values. Returns an ExpandResult that indicates whether all
   * variables were successfully resolved.
   *
   * @param input_json The JSON object to expand.
   * @param map Optional map of environment variables. If not provided,
   *            environment variables are read from the system.
   * @return An ExpandResult containing the expanded JSON and success status.
   */
  ExpandResult ExpandWithResult(json input_json,
                                std::optional<EnvMap> map = std::nullopt) const;

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

  /**
   * @brief Expand environment variables in a string (with result status).
   *
   * Expands environment variables in the format ${VAR_NAME} or $VAR_NAME
   * with their corresponding values. Returns an ExpandResult that indicates
   * whether all variables were successfully resolved.
   *
   * @param str The string containing environment variables to expand.
   * @param map Optional map of environment variables. If not provided,
   *            environment variables are read from the system.
   * @return An ExpandResult containing the expanded string and success status.
   */
  ExpandResult ExpandWithResult(const std::string& str,
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

  /**
   * @brief Expand a single environment variable reference (with result status).
   *
   * @param str The input string.
   * @param pos The starting position of the variable reference.
   * @param env_map The environment variable map to use for lookup.
   * @param expanded_str The output string to append expanded content to.
   * @param found Output parameter indicating if the variable was found.
   * @param var_name Output parameter containing the name of the variable being
   * expanded.
   * @return The position in the input string after the variable reference.
   */
  size_t ExpandVariableWithResult(const std::string& str, size_t pos,
                                  const EnvMap& env_map,
                                  std::string& expanded_str, bool& found,
                                  std::string& var_name) const;
};

}  // namespace assistant
