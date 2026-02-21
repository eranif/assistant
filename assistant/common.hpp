#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "assistant/attributes.hpp"
#include "common/json.hpp"

namespace assistant {
using json = nlohmann::ordered_json;

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

enum class Reason {
  /// The current reason completed successfully.
  kDone,
  /// More data to come.
  kPartialResult,
  /// A non recoverable error.
  kFatalError,
  /// Log messages - NOTICE
  kLogNotice,
  /// Log messages - DEBUG
  kLogDebug,
  /// Request cancelled by the user.
  kCancelled,
  /// Cost
  kRequestCost,
};

enum class ModelCapabilities {
  kNone = (0),
  kThinking = (1 << 0),
  kTools = (1 << 1),
  kCompletion = (1 << 2),
  kInsert = (1 << 3),
  kVision = (1 << 4),
};

/// Options passed to the "Chat()" API call.
enum class ChatOptions {
  /// Default. All is enabled.
  kDefault = (0),
  /// Do not pass the tools to the current chat request.
  kNoTools = (1 << 0),
  /// Do not pass the chat history to the current chat request.
  kNoHistory = (1 << 1),
};

enum class CachePolicy {
  /// No caching.
  kNone,
  /// Let the serice decide
  kAuto,
  /// Cache static content
  kStatic,
};

using OnResponseCallback = std::function<bool(
    const std::string& text, Reason call_reason, bool thinking)>;

/// Called when a tool is about to be invoked.
using OnToolInvokeCallback = std::function<bool(const std::string& tool_name)>;

/// Tokens pricing
struct Pricing {
  double input_tokens{0.0};                 // $ per 1 token
  double cache_creation_input_tokens{0.0};  // $ per 1 token
  double cache_read_input_tokens{0.0};      // $ per 1 token
  double output_tokens{0.0};                // $ per 1 token
};

struct Usage {
  int input_tokens{0};
  int cache_creation_input_tokens{0};
  int cache_read_input_tokens{0};
  int output_tokens{0};

  static Usage FromClaudeJson(json j) {
    Usage result;
    ReadNumber(j, "input_tokens", result.input_tokens);
    ReadNumber(j, "cache_creation_input_tokens",
               result.cache_creation_input_tokens);
    ReadNumber(j, "cache_read_input_tokens", result.cache_read_input_tokens);
    ReadNumber(j, "output_tokens", result.output_tokens);
    return result;
  }

  /**
   * @brief Adds the token usage counts from another Usage object to this
   * object.
   *
   * This method performs element-wise addition of all token counters,
   * accumulating the input tokens, cache creation tokens, cache read tokens,
   * and output tokens from the provided Usage object into the corresponding
   * fields of this instance.
   *
   * @param other The Usage object whose token counts will be added to this
   * object. The parameter is passed by const reference and remains unmodified.
   */
  Usage& Add(const Usage& other) {
    input_tokens += other.input_tokens;
    cache_creation_input_tokens += other.cache_creation_input_tokens;
    cache_read_input_tokens += other.cache_read_input_tokens;
    output_tokens += other.output_tokens;
    return *this;
  }

  /**
   * @brief Calculates the total monetary cost based on token usage.
   *
   * @details Computes the cost by multiplying each token count from the
   * provided Cost structure by the corresponding per-token rate stored in this
   * object, then summing all components (input, cache creation, cache read, and
   * output).
   *
   * @param cost A Cost structure containing the token counts for each category:
   *             input tokens, cache creation input tokens, cache read input
   * tokens, and output tokens.
   *
   * @return The total calculated cost as a double-precision floating-point
   * value, representing the sum of all token-based cost components.
   */
  double CalculateCost(const Pricing& cost) const {
    return (cost.input_tokens * static_cast<double>(input_tokens)) +
           (cost.cache_creation_input_tokens *
            static_cast<double>(cache_creation_input_tokens)) +
           (cost.cache_read_input_tokens *
            static_cast<double>(cache_read_input_tokens)) +
           (cost.output_tokens * static_cast<double>(output_tokens));
  }

 private:
  inline static void ReadNumber(const json& j, std::string_view name,
                                int& output) {
    if (j.contains(name) && j[name].is_number()) {
      output = j[name].get<int>();
    }
  }
};

// Initialized map with per-token prices (USD)
// All units are in: $ per 1 token
// input_tokens, cache_creation_input_tokens, cache_read_input_tokens,
// output_tokens
inline static std::unordered_map<std::string, Pricing> PRICING_TABLE = {
    {"claude-sonnet-4-6", {0.000003, 0.00000375, 0.0000003, 0.000015}},
    {"claude-opus-4-20250514", {0.000015, 0.00001875, 0.0000015, 0.000075}},
    {"claude-opus-4", {0.000015, 0.00001875, 0.0000015, 0.000075}},
    {"claude-sonnet-4", {0.000003, 0.00000375, 0.0000003, 0.000015}},
    {"claude-opus-4-5-20251101", {0.000005, 0.00000625, 0.0000005, 0.000025}},
    {"claude-opus-4-5", {0.000005, 0.00000625, 0.0000005, 0.000025}},
    {"claude-sonnet-4-5-20250929", {0.000003, 0.00000375, 0.0000003, 0.000015}},
    {"claude-sonnet-4-5", {0.000003, 0.00000375, 0.0000003, 0.000015}},
    {"claude-haiku-4-5-20251001", {0.000001, 0.00000125, 0.0000001, 0.000005}},
    {"claude-haiku-4-5", {0.000001, 0.00000125, 0.0000001, 0.000005}},
    {"claude-opus-4-6", {0.000005, 0.00000625, 0.0000005, 0.000025}}};
inline static std::mutex CLAUDE_PRICING_mutex;

inline std::optional<Pricing> FindPricing(const std::string& model_name) {
  std::lock_guard lock{CLAUDE_PRICING_mutex};
  auto iter = PRICING_TABLE.find(model_name);
  if (iter == PRICING_TABLE.end()) {
    return std::nullopt;
  }
  return iter->second;
}

inline void AddPricing(const std::string& model_name, const Pricing& pricing) {
  std::lock_guard lock{CLAUDE_PRICING_mutex};
  PRICING_TABLE.find(model_name);
  PRICING_TABLE.insert({model_name, pricing});
}

}  // namespace assistant
