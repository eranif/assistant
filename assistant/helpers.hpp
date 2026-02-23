#pragma once

#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "assistant/attributes.hpp"
#include "common/json.hpp"

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define UNIQUE_VAR(name) CONCAT(name, __LINE__)

#define ASSIGN_OPT_OR_RETURN(Decl, Expr, ReturnValue) \
  auto UNIQUE_VAR(__result) = (Expr);                 \
  if (!UNIQUE_VAR(__result).has_value()) {            \
    return ReturnValue;                               \
  }                                                   \
  Decl = UNIQUE_VAR(__result).value();

#define ASSIGN_OPT_OR_RETURN_NULLOPT(Decl, Expr) \
  ASSIGN_OPT_OR_RETURN(Decl, Expr, std::nullopt)

namespace assistant {
/**
 * @brief Joins the elements of a container into a single string with a
 * separator.
 *
 * @tparam Container A container type (e.g., std::vector, std::list,
 * std::array).
 * @param elements The container of elements to join.
 * @param separator The string used to separate the elements.
 * @return The joined string.
 */
template <typename Container>
std::string JoinArray(const Container& elements, const std::string& separator) {
  // Return an empty string immediately if the container is empty.
  if (elements.empty()) {
    return "";
  }

  std::ostringstream oss;

  // Use an iterator to handle all container types.
  auto it = elements.begin();

  // Append the first element without a separator.
  oss << "[" << *it;
  ++it;

  // Append the remaining elements with the separator.
  for (; it != elements.end(); ++it) {
    oss << separator << *it;
  }
  oss << "]";
  return oss.str();
}

template <typename T>
inline std::ostream& operator<<(std::ostream& o, const std::vector<T>& v) {
  o << JoinArray(v, ", ");
  return o;
}

inline std::string_view trim(const std::string_view& str) {
  // Find the first non-whitespace character
  size_t start = str.find_first_not_of(" \t\n\r\f\v");

  // If the string contains only whitespace, return empty string_view
  if (start == std::string_view::npos) {
    return "";
  }

  // Find the last non-whitespace character
  size_t end = str.find_last_not_of(" \t\n\r\f\v");

  // Return substring view between start and end positions
  return str.substr(start, end - start + 1);
}

/// This function returns a vector of all complete lines.
/// If the original string ends with an incomplete line,
/// that line is returned separately as a string.
inline std::pair<std::vector<std::string>, std::string> split_into_lines(
    const std::string& text, bool wants_empty_lines = false) {
  std::vector<std::string> complete_lines;
  std::string incomplete_line;
  std::istringstream stream(text);
  std::string line;

  // Use a while loop with std::getline to extract all lines
  while (std::getline(stream, line)) {
    auto trimmed_line = trim(line);
    if (!wants_empty_lines && trimmed_line.empty()) {
      continue;
    }
    complete_lines.push_back(line);
  }

  // After the loop, the stream's state can tell us about the last line.
  // If the stream reached EOF AND the last char of the original string
  // was not a newline, then the last line read was incomplete.
  if (!complete_lines.empty() && stream.eof() && text.back() != '\n') {
    incomplete_line = complete_lines.back();
    complete_lines.pop_back();  // Remove it from the complete lines vector
  }

  return {complete_lines, incomplete_line};
}

inline std::string_view after_first(const std::string_view& str,
                                    const std::string_view& delimiter) {
  size_t pos = str.find(delimiter);
  if (pos != std::string_view::npos) {
    return str.substr(pos + delimiter.length());
  }
  return "";  // Return empty string_view if delimiter is not found
}

/***
 * @brief Attempts to parse multiple JSON objects from a given string.
 *
 * The function reads JSON objects from the input string sequentially until
 * parsing fails or the end of the string is reached. It returns a pair
 * containing the vector of successfully parsed JSON objects and the remaining
 * unparsed portion of the string.
 *
 * @param instr The input string containing JSON objects.
 *
 * @return A pair where the first element is a vector of successfully parsed
 *         nlohmann::ordered_json objects and the second element is the
 *         remainder of the input string after the last successful parse.
 *
 * @throws None.
 */
inline std::pair<std::vector<nlohmann::ordered_json>, std::string>
try_read_jsons_from_string(const std::string& instr) {
  std::vector<nlohmann::ordered_json> result;
  std::stringstream ss{instr};
  std::streampos last_good_pos = 0;

  while (true) {
    try {
      nlohmann::ordered_json j;

      // Save position before parsing attempt
      std::streampos pos_before = ss.tellg();
      if (pos_before == -1) {
        pos_before = 0;
      }

      ss >> j;

      // Check if parsing succeeded (stream is still good)
      if (ss.fail()) {
        break;
      }

      result.push_back(std::move(j));

      // Update last good position after successful parse
      last_good_pos = ss.tellg();
      if (last_good_pos == -1) {
        // End of stream reached
        last_good_pos = instr.size();
        break;
      }

    } catch (const nlohmann::json::exception& e) {
      // JSON parsing threw an exception, stop here
      break;
    } catch (...) {
      // Unexpected exception, stop parsing
      break;
    }
  }

  // Extract remainder from the last successful position
  std::string remainder = instr.substr(static_cast<size_t>(last_good_pos));

  return {result, remainder};
}

}  // namespace assistant
