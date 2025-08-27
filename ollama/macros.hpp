#pragma once

#define ENUM_CLASS_BITWISE_OPERATOS(EnumName)                            \
  inline EnumName operator|(EnumName a, EnumName b) {                    \
    using T = std::underlying_type_t<EnumName>;                          \
    return static_cast<EnumName>(static_cast<T>(a) | static_cast<T>(b)); \
  }                                                                      \
  inline EnumName& operator|=(EnumName& a, EnumName b) {                 \
    a = a | b;                                                           \
    return a;                                                            \
  }                                                                      \
                                                                         \
  inline EnumName operator&(EnumName a, EnumName b) {                    \
    using T = std::underlying_type_t<EnumName>;                          \
    return static_cast<EnumName>(static_cast<T>(a) & static_cast<T>(b)); \
  }                                                                      \
                                                                         \
  inline bool IsFlagSet(EnumName flags, EnumName flag) {                 \
    return (flags & flag) == flag;                                       \
  }
