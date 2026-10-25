#include <recomp/native/wait_set.h>

namespace recomp::native {
std::optional<size_t> WaitSet::WaitUntil(const Probe &probe,
                                         Clock::time_point deadline) {
  std::unique_lock lock(mutex_);
  ++statistics_.waits;
  for (;;) {
    const auto observed = generation_;
    ++statistics_.probes;
    lock.unlock();
    auto ready = probe();
    lock.lock();
    if (ready)
      return ready;
    if (generation_ != observed)
      continue;
    if (deadline != Clock::time_point::max() && Clock::now() >= deadline) {
      ++statistics_.timeouts;
      return std::nullopt;
    }
    ++statistics_.parks;
    const auto changed = [&] { return generation_ != observed; };
    if (deadline == Clock::time_point::max()) {
      changed_.wait(lock, changed);
    } else if (!changed_.wait_until(lock, deadline, changed)) {
      ++statistics_.timeouts;
      return std::nullopt;
    }
  }
}

void WaitSet::Notify() {
  {
    std::lock_guard lock(mutex_);
    ++generation_;
    ++statistics_.notifications;
  }
  changed_.notify_all();
}

WaitSet::Statistics WaitSet::GetStatistics() const {
  std::lock_guard lock(mutex_);
  return statistics_;
}
} // namespace recomp::native
