#pragma once

#include <type_traits>

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
