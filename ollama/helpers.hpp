#pragma once

#include <sstream>
#include <string>

namespace ollama {
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
}  // namespace ollama