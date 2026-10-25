#pragma once
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

namespace recomp::native {
// Notification-driven waiting for caller-owned host state. The probe checks
// readiness and atomically consumes one result under the caller's state locks.
// It runs outside this object's mutex and may return an interrupt/cancel index.
// Every producer must publish state before Notify(). This includes callbacks,
// shutdown and any transition that can make the probe succeed. Notify is not
// async-signal-safe. Callers own state, selection policy and payload lifetime.
class WaitSet {
public:
  using Clock = std::chrono::steady_clock;
  using Probe = std::function<std::optional<size_t>()>;
  struct Statistics {
    uint64_t waits = 0, probes = 0, parks = 0, notifications = 0, timeouts = 0;
  };
  // max() means infinite. A ready probe wins even at an expired deadline;
  // nullopt means timeout. A producer racing the probe-to-park boundary cannot
  // lose its wakeup. Spurious wakes do not rerun the probe. Probe exceptions
  // propagate. Join all waiters/producers before destroying the object.
  std::optional<size_t> WaitUntil(const Probe &, Clock::time_point deadline);
  void Notify();
  Statistics GetStatistics() const;

private:
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  uint64_t generation_ = 0;
  Statistics statistics_;
};
} // namespace recomp::native
