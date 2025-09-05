#pragma once

#include <optional>
#include <string>

#include "ollama/ollamalib.hpp"

namespace ollama {
struct ModelOptions {
  std::string name;
  json options;
  std::optional<bool> think{std::nullopt};
};

}  // namespace ollama