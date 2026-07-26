#include <gtest/gtest.h>

#include <chrono>

#include "assistant/client/retry_policy.hpp"

using namespace assistant;
using namespace std::chrono;

namespace {
RetryContext MakeCtx(size_t attempt, int status, std::string msg = "") {
  RetryContext ctx;
  ctx.attempt = attempt;
  ctx.http_status = status;
  ctx.error_message = std::move(msg);
  return ctx;
}
}  // namespace

TEST(ExponentialBackoffRetryPolicy, RetriesRateLimitStatuses) {
  ExponentialBackoffRetryPolicy policy;
  EXPECT_TRUE(policy.ShouldRetry(MakeCtx(0, 429)));
  EXPECT_TRUE(policy.ShouldRetry(MakeCtx(0, 529)));
}

TEST(ExponentialBackoffRetryPolicy, RetriesServerErrorsByDefault) {
  ExponentialBackoffRetryPolicy policy;
  EXPECT_TRUE(policy.ShouldRetry(MakeCtx(0, 500)));
  EXPECT_TRUE(policy.ShouldRetry(MakeCtx(0, 503)));
}

TEST(ExponentialBackoffRetryPolicy, DoesNotRetryServerErrorsWhenDisabled) {
  ExponentialBackoffRetryPolicy::Options opts;
  opts.retry_on_server_error = false;
  ExponentialBackoffRetryPolicy policy{opts};
  EXPECT_FALSE(policy.ShouldRetry(MakeCtx(0, 500)));
  // But rate limits are still retried.
  EXPECT_TRUE(policy.ShouldRetry(MakeCtx(0, 429)));
}

TEST(ExponentialBackoffRetryPolicy, DoesNotRetryPermanentClientErrors) {
  ExponentialBackoffRetryPolicy policy;
  EXPECT_FALSE(policy.ShouldRetry(MakeCtx(0, 400)));
  EXPECT_FALSE(policy.ShouldRetry(MakeCtx(0, 401)));
  EXPECT_FALSE(policy.ShouldRetry(MakeCtx(0, 404)));
}

TEST(ExponentialBackoffRetryPolicy, StopsAfterMaxRetries) {
  ExponentialBackoffRetryPolicy::Options opts;
  opts.max_retries = 3;
  ExponentialBackoffRetryPolicy policy{opts};
  EXPECT_TRUE(policy.ShouldRetry(MakeCtx(0, 429)));
  EXPECT_TRUE(policy.ShouldRetry(MakeCtx(2, 429)));
  EXPECT_FALSE(policy.ShouldRetry(MakeCtx(3, 429)));
  EXPECT_FALSE(policy.ShouldRetry(MakeCtx(4, 429)));
}

TEST(ExponentialBackoffRetryPolicy, ReportsMaxRetries) {
  ExponentialBackoffRetryPolicy::Options opts;
  opts.max_retries = 7;
  ExponentialBackoffRetryPolicy policy{opts};
  ASSERT_TRUE(policy.GetMaxRetries().has_value());
  EXPECT_EQ(*policy.GetMaxRetries(), 7u);
}

TEST(ExponentialBackoffRetryPolicy, FallsBackToMessageWhenStatusUnknown) {
  ExponentialBackoffRetryPolicy policy;
  EXPECT_TRUE(
      policy.ShouldRetry(MakeCtx(0, 0, "Error: rate limit exceeded")));
  EXPECT_TRUE(policy.ShouldRetry(MakeCtx(0, 0, "server is Overloaded")));
  EXPECT_TRUE(policy.ShouldRetry(MakeCtx(0, 0, "HTTP 429 Too Many Requests")));
  EXPECT_FALSE(
      policy.ShouldRetry(MakeCtx(0, 0, "connection refused by host")));
}

TEST(ExponentialBackoffRetryPolicy, DelayGrowsExponentiallyAndCaps) {
  ExponentialBackoffRetryPolicy::Options opts;
  opts.base_delay = milliseconds(100);
  opts.multiplier = 2.0;
  opts.max_delay = milliseconds(500);
  opts.jitter = 0.0;  // deterministic
  ExponentialBackoffRetryPolicy policy{opts};

  EXPECT_EQ(policy.GetDelay(MakeCtx(0, 429)), milliseconds(100));
  EXPECT_EQ(policy.GetDelay(MakeCtx(1, 429)), milliseconds(200));
  EXPECT_EQ(policy.GetDelay(MakeCtx(2, 429)), milliseconds(400));
  // 800 would exceed the cap, so it is clamped to max_delay.
  EXPECT_EQ(policy.GetDelay(MakeCtx(3, 429)), milliseconds(500));
  EXPECT_EQ(policy.GetDelay(MakeCtx(10, 429)), milliseconds(500));
}

TEST(ExponentialBackoffRetryPolicy, JitterStaysWithinBounds) {
  ExponentialBackoffRetryPolicy::Options opts;
  opts.base_delay = milliseconds(1000);
  opts.multiplier = 1.0;  // constant base for easy bounds
  opts.max_delay = milliseconds(10000);
  opts.jitter = 0.5;
  ExponentialBackoffRetryPolicy policy{opts};

  for (int i = 0; i < 100; ++i) {
    auto d = policy.GetDelay(MakeCtx(0, 429));
    EXPECT_GE(d, milliseconds(500));
    EXPECT_LE(d, milliseconds(1000));
  }
}
