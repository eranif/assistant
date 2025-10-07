#pragma once

#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "assistant/attributes.hpp"

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

template <typename EnumName>
inline bool IsFlagSet(EnumName flags, EnumName flag) {
  using T = std::underlying_type_t<EnumName>;
  return (static_cast<T>(flags) & static_cast<T>(flag)) == static_cast<T>(flag);
}

template <typename EnumName>
inline void AddFlagSet(EnumName& flags, EnumName flag) {
  using T = std::underlying_type_t<EnumName>;
  T& t = reinterpret_cast<T&>(flags);
  t |= static_cast<T>(flag);
}

/// A helper class that construct ValueType and the provide access to it only
/// via the "with_mut" and "with" methods.
template <typename ValueType>
class Locker {
 public:
  template <typename... Args>
  Locker(Args... args) : m_value(std::forward<Args>(args)...) {}

  Locker(const Locker& other) = delete;
  Locker& operator=(const Locker& other) = delete;

  /// Provide a write access to the underlying type.
  void with_mut(std::function<void(ValueType&)> cb) {
    std::scoped_lock lk{m_mutex};
    cb(m_value);
  }

  /// Provide a read-only access to the underlying type.
  void with(std::function<void(const ValueType&)> cb) const {
    std::scoped_lock lk{m_mutex};
    cb(m_value);
  }

  /// Return a **copy of the value**
  ValueType get_value() const {
    std::scoped_lock lk{m_mutex};
    return m_value;
  }

  /// set the value.
  void set_value(ValueType value) {
    std::scoped_lock lk{m_mutex};
    m_value = std::move(value);
  }

 private:
  mutable std::mutex m_mutex;
  ValueType m_value GUARDED_BY(m_mutex);
};

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

}  // namespace assistant
