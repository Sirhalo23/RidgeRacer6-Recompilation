#include <array>
#include <atomic>
#include <future>
#include <iostream>
#include <latch>
#include <recomp/native/wait_set.h>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
using recomp::native::WaitSet;
namespace {
void Check(bool ok) {
  if (!ok)
    throw std::runtime_error("Native wait set assertion failed");
}
struct State {
  WaitSet wait;
  std::mutex mutex;
  std::array<unsigned, 3> count{};
  bool manual = false, cancel = false;
  void Add(size_t index, unsigned n = 1) {
    {
      std::lock_guard lock(mutex);
      count[index] += n;
    }
    wait.Notify();
  }
  std::optional<size_t> Probe() {
    std::lock_guard lock(mutex);
    if (cancel)
      return 99;
    for (size_t i = 0; i < count.size(); ++i) {
      if (!count[i])
        continue;
      if (!(manual && i == 2))
        --count[i];
      return i;
    }
    return std::nullopt;
  }
  std::optional<size_t> Take() {
    return wait.WaitUntil([&] { return Probe(); }, WaitSet::Clock::now() + 2s);
  }
};
void ReadyAndTimeouts() {
  State s;
  s.Add(1, 2);
  s.Add(0);
  Check(s.Take() == 0 && s.Take() == 1 && s.Take() == 1);
  Check(!s.wait.WaitUntil([&] { return s.Probe(); }, WaitSet::Clock::now()));
  s.manual = true;
  s.Add(2);
  Check(s.Take() == 2 && s.Take() == 2);
  // Readiness wins at an expired deadline. No timeout arithmetic or spinning.
  Check(s.wait.WaitUntil([&] { return s.Probe(); },
                         WaitSet::Clock::now() - 1s) == 2);
  const auto stats = s.wait.GetStatistics();
  Check(stats.parks == 0 && stats.timeouts == 1);
}
void ProbeToParkRace() {
  State s;
  std::latch checked(1), published(1);
  std::atomic<bool> first = true;
  auto future = std::async(std::launch::async, [&] {
    return s.wait.WaitUntil(
        [&]() -> std::optional<size_t> {
          if (first.exchange(false)) {
            Check(!s.Probe());
            checked.count_down();
            published.wait();
            return std::nullopt; // Producer notified after this readiness
                                 // snapshot.
          }
          return s.Probe();
        },
        WaitSet::Clock::now() + 2s);
  });
  checked.wait();
  s.Add(0);
  published.count_down();
  Check(future.get() == 0);
  Check(s.wait.GetStatistics().probes == 2 &&
        s.wait.GetStatistics().parks == 0);
}
void MultipleConsumers() {
  State s;
  std::latch started(2);
  auto launch = [&] {
    return std::async(std::launch::async, [&] {
      bool first = true;
      return s.wait.WaitUntil(
          [&] {
            if (first) {
              first = false;
              started.count_down();
            }
            return s.Probe();
          },
          WaitSet::Clock::now() + 2s);
    });
  };
  auto a = launch(), b = launch();
  started.wait();
  s.Add(1, 2);
  Check(a.get() == 1 && b.get() == 1 && !s.Probe());
}
void CancellationAndNoPolling() {
  State s;
  std::atomic<unsigned> probes = 0;
  std::latch initial(1);
  auto future = std::async(std::launch::async, [&] {
    return s.wait.WaitUntil(
        [&] {
          if (++probes == 1)
            initial.count_down();
          return s.Probe();
        },
        WaitSet::Clock::time_point::max());
  });
  initial.wait();
  std::this_thread::sleep_for(20ms);
  Check(probes == 1); // No timed polling of an unchanged predicate.
  {
    std::lock_guard lock(s.mutex);
    s.cancel = true;
  }
  s.wait.Notify();
  Check(future.get() == 99);
}
void CallbackReentryAndExceptions() {
  State s;
  // A probe may run a callback that publishes/notifications into the same set.
  bool callback = true;
  Check(s.wait.WaitUntil(
            [&]() -> std::optional<size_t> {
              if (callback) {
                callback = false;
                s.Add(0);
                return std::nullopt;
              }
              return s.Probe();
            },
            WaitSet::Clock::now() + 2s) == 0);
  bool threw = false;
  try {
    s.wait.WaitUntil(
        []() -> std::optional<size_t> { throw std::runtime_error("probe"); },
        WaitSet::Clock::now() + 2s);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  s.Add(1);
  Check(threw && s.Take() == 1);
}
void Stress() {
  State s;
  constexpr unsigned n = 4000;
  auto consumer = std::async(std::launch::async, [&] {
    for (unsigned i = 0; i < n; ++i)
      Check(s.Take() == i % 3);
  });
  // One outstanding token at a time verifies consumption and ordering.
  for (unsigned i = 0; i < n; ++i) {
    for (;;) {
      {
        std::lock_guard lock(s.mutex);
        if (s.count[0] + s.count[1] + s.count[2] == 0)
          break;
      }
      std::this_thread::yield();
    }
    s.Add(i % 3);
  }
  consumer.get();
  Check(!s.Probe());
}
} // namespace
int main() {
  try {
    ReadyAndTimeouts();
    ProbeToParkRace();
    MultipleConsumers();
    CancellationAndNoPolling();
    CallbackReentryAndExceptions();
    Stress();
    std::cout << "Native wait set: readiness, deadlines, lost wakeups, "
                 "consumers, cancellation, callbacks and stress passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
