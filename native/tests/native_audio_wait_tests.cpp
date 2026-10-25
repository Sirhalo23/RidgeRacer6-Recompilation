#include "native_audio_wait.h"
#include <array>
#include <atomic>
#include <future>
#include <iostream>
#include <latch>
#include <rex/logging.h>
#include <rex/thread.h>
#include <stdexcept>
#include <thread>
using namespace std::chrono_literals;
using namespace rex::thread;
namespace {
void Check(bool ok) {
  if (!ok)
    throw std::runtime_error("Native audio adapter assertion failed");
}
struct Handles {
  std::array<std::unique_ptr<Semaphore>, 8> sem;
  std::unique_ptr<Event> shutdown;
  std::array<WaitHandle *, 9> handles;
  explicit Handles(bool manual = false) {
    for (size_t i = 0; i < sem.size(); ++i) {
      sem[i] = Semaphore::Create(0, 8);
      handles[i] = sem[i].get();
    }
    shutdown = manual ? Event::CreateManualResetEvent(false)
                      : Event::CreateAutoResetEvent(false);
    handles[8] = shutdown.get();
  }
  auto Take(bool alertable = true, std::chrono::milliseconds timeout = 2s) {
    return WaitAny(handles.data(), handles.size(), alertable, timeout);
  }
};
void Join(Thread *thread) {
  Check(Wait(thread, false, 3s) == WaitResult::kSuccess);
}
void AwaitPark(const std::shared_ptr<recomp::native::WaitSet> &gate) {
  const auto until = std::chrono::steady_clock::now() + 2s;
  while (!gate->GetStatistics().parks &&
         std::chrono::steady_clock::now() < until)
    std::this_thread::yield();
  Check(gate->GetStatistics().parks > 0);
}
void SelectionAndCounts(bool native) {
  rr6::NativeAudioWaitScope scope(native);
  Handles h;
  int previous = -1;
  Check(h.sem[1]->Release(2, &previous) && previous == 0);
  Check(h.sem[0]->Release(1, nullptr));
  Check(h.Take().second == 0 && h.Take().second == 1 && h.Take().second == 1);
  Check(h.Take(false, 0ms).first == WaitResult::kTimeout);
  Check(h.sem[2]->Release(8, &previous) && previous == 0);
  Check(!h.sem[2]->Release(1, nullptr));
  for (int i = 0; i < 8; ++i) {
    const auto r = h.Take();
    Check(r.first == WaitResult::kSuccess && r.second == 2);
  }
  Check(h.Take(false, 0ms).first == WaitResult::kTimeout);
  if (native)
    Check(rr6::CurrentNativeAudioWaitSet()->GetStatistics().waits == 13);
}
void ManualEventAndTimeout() {
  rr6::NativeAudioWaitScope scope(true);
  Handles h(true);
  h.shutdown->Set();
  Check(h.Take().second == 8 && h.Take().second == 8);
  h.shutdown->Reset();
  Check(h.Take(false, 8ms).first == WaitResult::kTimeout);
  auto s = rr6::CurrentNativeAudioWaitSet()->GetStatistics();
  Check(s.timeouts == 1 && s.parks > 0);
  Check(h.sem[0]->Release(1, nullptr));
  // Preserve the original alertable-zero-time behavior, including no consume.
  Check(h.Take(true, 0ms).first == WaitResult::kTimeout);
  Check(h.Take(false, 0ms).first == WaitResult::kSuccess);
}
void AlreadyQueuedCallbacks() {
  rr6::NativeAudioWaitScope scope(true);
  Handles h;
  auto *self = Thread::GetCurrentThread();
  int order = 0;
  self->QueueUserCallback([&] {
    Check(order++ == 0);
    h.sem[0]->Release(1, nullptr);
  });
  self->QueueUserCallback([&] { Check(order++ == 1); });
  Check(h.Take().first == WaitResult::kUserCallback);
  Check(h.Take().first == WaitResult::kUserCallback);
  Check(h.Take().first == WaitResult::kSuccess && order == 2);
}
void CrossThreadCallbacksAndShutdown() {
  Handles h;
  std::promise<std::shared_ptr<recomp::native::WaitSet>> ready;
  std::latch callback_entered(1), callback_continue(1);
  std::atomic<int> order = 0;
  std::atomic<bool> passed = false;
  auto worker = Thread::Create({}, [&] {
    rr6::NativeAudioWaitScope scope(true);
    ready.set_value(rr6::CurrentNativeAudioWaitSet());
    const auto a = h.Take(true, std::chrono::milliseconds::max());
    const auto b = h.Take();
    const auto c = h.Take();
    passed = a.first == WaitResult::kUserCallback &&
             b.first == WaitResult::kUserCallback &&
             c.first == WaitResult::kSuccess && c.second == 8 && order == 2;
  });
  Check(bool(worker));
  auto gate = ready.get_future().get();
  AwaitPark(gate);
  worker->QueueUserCallback([&] {
    ++order;
    callback_entered.count_down();
    callback_continue.wait();
  });
  callback_entered.wait();
  worker->QueueUserCallback([&] { ++order; });
  h.shutdown->Set();
  callback_continue.count_down();
  Join(worker.get());
  Check(passed);
}
void PauseResume() {
  Handles h;
  auto resume = Event::CreateAutoResetEvent(false);
  std::promise<void> ready, paused;
  std::atomic<bool> passed = false;
  auto worker = Thread::Create({}, [&] {
    rr6::NativeAudioWaitScope scope(true);
    ready.set_value();
    auto pause = h.Take();
    paused.set_value();
    auto resumed = Wait(resume.get(), false, 2s);
    auto frame = h.Take();
    auto stop = h.Take();
    passed = pause.first == WaitResult::kSuccess && pause.second == 8 &&
             resumed == WaitResult::kSuccess && frame.second == 3 &&
             frame.first == WaitResult::kSuccess && stop.second == 8 &&
             stop.first == WaitResult::kSuccess;
  });
  Check(bool(worker));
  ready.get_future().get();
  h.shutdown->Set();
  paused.get_future().get();
  h.sem[3]->Release(1, nullptr);
  resume->Set();
  // Lowest-index frame wins over simultaneous shutdown, matching SDK selection.
  h.shutdown->Set();
  Join(worker.get());
  Check(passed);
}
void SignalStressAndScopeIsolation() {
  Handles h;
  auto ack = Event::CreateAutoResetEvent(false);
  std::promise<std::shared_ptr<recomp::native::WaitSet>> ready;
  std::atomic<bool> passed = true;
  auto worker = Thread::Create({}, [&] {
    rr6::NativeAudioWaitScope scope(true);
    ready.set_value(rr6::CurrentNativeAudioWaitSet());
    for (size_t i = 0; i < 512; ++i) {
      auto result = h.Take();
      if (result.first != WaitResult::kSuccess || result.second != i % 8)
        passed = false;
      ack->Set();
    }
  });
  Check(bool(worker));
  auto gate = ready.get_future().get();
  AwaitPark(gate);
  // A second domain must fall back rather than overwrite a live subscription.
  {
    rr6::NativeAudioWaitScope other(true);
    Check(h.Take(false, 0ms).first == WaitResult::kTimeout);
    Check(rr6::CurrentNativeAudioWaitSet()->GetStatistics().waits == 0);
  }
  for (size_t i = 0; i < 512; ++i) {
    Check(h.sem[i % 8]->Release(1, nullptr));
    Check(Wait(ack.get(), false, 2s) == WaitResult::kSuccess);
  }
  Join(worker.get());
  Check(passed && gate->GetStatistics().waits == 512);
  for (auto &sem : h.sem)
    Check(Wait(sem.get(), false, 0ms) == WaitResult::kTimeout);
  Check(!rr6::CurrentNativeAudioWaitSet());
}
void UnsupportedShapesFallback() {
  rr6::NativeAudioWaitScope scope(true);
  Handles h;
  Check(h.sem[1]->Release(1, nullptr));
  Check(WaitAny(h.handles.data(), 2, false, 0ms).second == 1);
  Check(rr6::CurrentNativeAudioWaitSet()->GetStatistics().waits == 0);
}
} // namespace
int main() {
  rex::InitLogging(nullptr, spdlog::level::warn);
  try {
    SelectionAndCounts(false);
    SelectionAndCounts(true);
    ManualEventAndTimeout();
    AlreadyQueuedCallbacks();
    CrossThreadCallbacksAndShutdown();
    PauseResume();
    SignalStressAndScopeIsolation();
    UnsupportedShapesFallback();
    std::cout
        << "Actual SDK handles: native activation, counts, selection, "
           "callbacks, shutdown, pause/resume, stress and fallback passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
