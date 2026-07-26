#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <random>
#include <string>

namespace assistant {

/// Describes a single failed request attempt. It is handed to the active
/// RetryPolicy so the policy can decide whether to retry and how long to wait.
struct RetryContext {
  /// Zero-based index of the attempt that just failed (0 == the first try).
  size_t attempt{0};
  /// HTTP status code associated with the failure, or 0 when unknown (for
  /// example when the failure came from the `curl` transport or a non-HTTP
  /// error).
  int http_status{0};
  /// The error message reported by the transport / exception.
  std::string error_message;
};

/// Pluggable retry policy. Whenever a request fails, the client consults the
/// active policy to decide whether the failure should trigger another attempt
/// and, if so, how long to back off first.
///
/// Provide your own behaviour by subclassing this interface and installing it
/// with `ClientBase::SetRetryPolicy(...)`. Install `nullptr` to disable
/// retries entirely.
class RetryPolicy {
 public:
  virtual ~RetryPolicy() = default;

  /// Return true if the failed attempt described by `ctx` should be retried.
  virtual bool ShouldRetry(const RetryContext& ctx) const = 0;

  /// The delay to wait before the next attempt, given the just-failed attempt
  /// described by `ctx`. Only consulted when `ShouldRetry(ctx)` returned true.
  virtual std::chrono::milliseconds GetDelay(const RetryContext& ctx) const = 0;

  /// The maximum number of retries this policy will perform, when it is a
  /// fixed, known bound. Used purely for human-readable progress messages
  /// (e.g. "Retrying 2 / 5"). Return `std::nullopt` for open-ended policies
  /// (for example one that retries based on a time budget).
  virtual std::optional<size_t> GetMaxRetries() const { return std::nullopt; }
};

/// Default retry policy: exponential backoff triggered on "limit" style
/// failures (HTTP 429 "too many requests", 529 "overloaded" and, optionally,
/// 5xx server errors). When the HTTP status is unknown, the error message is
/// matched against a few common rate-limit phrases so the policy also works
/// with the `curl` transport.
class ExponentialBackoffRetryPolicy : public RetryPolicy {
 public:
  struct Options {
    /// Maximum number of retries (i.e. attempts after the initial one).
    size_t max_retries{5};
    /// Delay before the first retry.
    std::chrono::milliseconds base_delay{std::chrono::seconds(1)};
    /// Upper bound for any single backoff delay.
    std::chrono::milliseconds max_delay{std::chrono::seconds(60)};
    /// Growth factor applied per attempt (delay = base_delay * multiplier^n).
    double multiplier{2.0};
    /// Fraction of jitter to apply, in the range [0.0, 1.0]. The final delay
    /// is scaled by a random factor in `[1 - jitter, 1]`. 0.0 disables jitter
    /// (and keeps delays deterministic, which is convenient for tests).
    double jitter{0.0};
    /// Retry on 5xx server errors in addition to the rate-limit statuses.
    bool retry_on_server_error{true};
  };

  ExponentialBackoffRetryPolicy() = default;
  explicit ExponentialBackoffRetryPolicy(Options opts) : m_opts(opts) {}

  bool ShouldRetry(const RetryContext& ctx) const override;
  std::chrono::milliseconds GetDelay(const RetryContext& ctx) const override;
  std::optional<size_t> GetMaxRetries() const override {
    return m_opts.max_retries;
  }

  const Options& GetOptions() const { return m_opts; }

 private:
  Options m_opts;
  mutable std::mutex m_rng_mutex;
  mutable std::mt19937 m_rng{0xC0FFEE};
};

}  // namespace assistant
