#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace ollama {

template <typename Value>
struct ThreadNotifier {
 public:
  ThreadNotifier() = default;
  ~ThreadNotifier() = default;

  // A non copyable class.
  ThreadNotifier(const ThreadNotifier&) = delete;
  ThreadNotifier& operator=(const ThreadNotifier&) = delete;

  /// Wait `milliseconds` until:
  /// 1. Timeout reached. the calling process will receive "nullopt"
  /// 2. Another thread notified, in this case the waiter will receive the
  /// `Value`.
  std::optional<Value> Wait(size_t milliseconds) {
    std::unique_lock lock{mutex_};
    auto timeout = std::chrono::milliseconds(milliseconds);

    // Wait for either notification or timeout
    if (cv_.wait_for(lock, timeout, [this] { return notified_.has_value(); })) {
      Value v = std::move(notified_.value());
      notified_.reset();
      return v;
    }
    return std::nullopt;
  }

  void Notify(Value v) {
    std::lock_guard lock{mutex_};
    notified_ = std::move(v);
    cv_.notify_all();  // Wake up all waiting threads
  }

 private:
  std::condition_variable cv_;
  std::mutex mutex_;
  std::optional<Value> notified_;
};

}  // namespace ollama