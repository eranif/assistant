#include "assistant/client/retry_policy.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace assistant {
namespace {
/// Case-insensitive substring search.
bool ContainsIgnoreCase(const std::string& haystack, const std::string& needle) {
  auto it = std::search(
      haystack.begin(), haystack.end(), needle.begin(), needle.end(),
      [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
      });
  return it != haystack.end();
}

/// Best-effort detection of a rate-limit / overloaded condition from a free
/// text error message. Used when the HTTP status code is not available.
bool LooksLikeLimitError(const std::string& message) {
  static const char* kNeedles[] = {
      "rate limit", "too many requests", "overloaded",
      "quota",      "429",              "529",
  };
  for (const char* needle : kNeedles) {
    if (ContainsIgnoreCase(message, needle)) {
      return true;
    }
  }
  return false;
}
}  // namespace

bool ExponentialBackoffRetryPolicy::ShouldRetry(const RetryContext& ctx) const {
  if (ctx.attempt >= m_opts.max_retries) {
    return false;
  }

  if (ctx.http_status == 429 || ctx.http_status == 529) {
    // Too many requests / overloaded: always a retry candidate.
    return true;
  }

  if (m_opts.retry_on_server_error && ctx.http_status >= 500 &&
      ctx.http_status < 600) {
    return true;
  }

  // 4xx (other than 429) are client errors that will not succeed on retry.
  if (ctx.http_status >= 400 && ctx.http_status < 500) {
    return false;
  }

  // Unknown status (e.g. curl transport or a raw exception): fall back to
  // pattern-matching the error message.
  if (ctx.http_status == 0) {
    return LooksLikeLimitError(ctx.error_message);
  }

  return false;
}

std::chrono::milliseconds ExponentialBackoffRetryPolicy::GetDelay(
    const RetryContext& ctx) const {
  double base = static_cast<double>(m_opts.base_delay.count());
  double factor = std::pow(m_opts.multiplier, static_cast<double>(ctx.attempt));
  double delay = base * factor;

  double capped = std::min(delay, static_cast<double>(m_opts.max_delay.count()));

  if (m_opts.jitter > 0.0) {
    double jitter = std::clamp(m_opts.jitter, 0.0, 1.0);
    double factor_min = 1.0 - jitter;
    std::scoped_lock lk{m_rng_mutex};
    std::uniform_real_distribution<double> dist(factor_min, 1.0);
    capped *= dist(m_rng);
  }

  return std::chrono::milliseconds(static_cast<long long>(capped));
}

}  // namespace assistant
