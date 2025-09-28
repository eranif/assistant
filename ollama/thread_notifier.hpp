#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace ollama {
enum class WakeupReason {
  kTimeout,   /// Timeout occurred
  kNotified,  /// Someone notified the thread
};

struct ThreadNotifier {
 public:
  ThreadNotifier() = default;
  ~ThreadNotifier() = default;

  // A non copyable class.
  ThreadNotifier(const ThreadNotifier&) = delete;
  ThreadNotifier& operator=(const ThreadNotifier&) = delete;

  WakeupReason Wait(size_t milliseconds) {
    std::unique_lock lock{mutex_};
    auto timeout = std::chrono::milliseconds(milliseconds);

    // Wait for either notification or timeout
    if (cv_.wait_for(lock, timeout, [this] { return notified_; })) {
      notified_ = false;
      return WakeupReason::kNotified;
    }
    return WakeupReason::kTimeout;
  }

  void Notify() {
    std::lock_guard lock{mutex_};
    notified_ = true;
    cv_.notify_all();  // Wake up all waiting threads
  }

 private:
  std::condition_variable cv_;
  std::mutex mutex_;
  bool notified_{false};
};

}  // namespace ollama