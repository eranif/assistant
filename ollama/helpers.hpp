#pragma once

#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "ollama/attributes.hpp"

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
}  // namespace ollama
