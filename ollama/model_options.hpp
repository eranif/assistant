#pragma once

#include <optional>
#include <ostream>
#include <string>

#include "ollama/ollamalib.hpp"

namespace assistant {
struct ModelOptions {
  std::string name;
  json options;
  std::optional<bool> think{std::nullopt};
  std::optional<bool> hidethinking{std::nullopt};
  std::string think_start_tag{"<think>"};
  std::string think_end_tag{"</think>"};
};

inline std::ostream& operator<<(std::ostream& os, const ModelOptions& mo) {
  os << "ModelOptions{name=" << mo.name << ", think="
     << (mo.think.has_value()
             ? (mo.think.value() ? std::string{"true"} : std::string{"false"})
             : std::string{"None"})
     << ", think_start_tag=" << mo.think_start_tag
     << ", think_end_tag=" << mo.think_end_tag << "}";
  return os;
}
}  // namespace assistant
