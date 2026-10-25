#!/usr/bin/env python3
"""Prepare pinned, build-local audio/wait integration; never edit the SDK checkout."""
import hashlib
from pathlib import Path
import sys


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('Ambiguous native audio integration anchor: ' + old[:80])
    return source.replace(old, new)


def replace_in(source, start, end, old, new):
    a = source.index(start)
    b = source.index(end, a)
    return source[:a] + replace_once(source[a:b], old, new) + source[b:]


def prepare_threading(source):
    if hashlib.sha256(source.encode()).hexdigest() != 'f60dfd4dc854f61c989bb68ba9dbc3c4cc6d3f58a6f2f00a19b2019538447fa9':
        raise ValueError('Unsupported SDK POSIX threading revision; review producer/consumer and callback semantics')
    source = '#include "native_audio_wait.h"\n' + source
    methods = '''  // All registrations and notifications use the existing condition lock.
  std::unique_lock<std::mutex> NativeAudioLock() {
#if REX_PLATFORM_LINUX
    auto *native = static_cast<pthread_mutex_t*>(mutex_.native_handle());
    int result = pthread_mutex_lock(native);
    if (result == EOWNERDEAD) pthread_mutex_consistent(native);
    else if (result != 0) return std::unique_lock<std::mutex>(mutex_, std::defer_lock);
    return std::unique_lock<std::mutex>(mutex_, std::adopt_lock);
#else
    return std::unique_lock<std::mutex>(mutex_);
#endif
  }
  bool SubscribeNativeAudio(const std::shared_ptr<recomp::native::WaitSet>& wake) {
    auto lock = NativeAudioLock();
    if (!lock.owns_lock()) return false;
    if (auto previous = native_audio_wait_.lock(); previous && previous != wake) return false;
    native_audio_wait_ = wake;
    return true;
  }
  // Caller holds mutex_. Publish the signal/count/callback before notifying.
  void NotifyNativeAudio() {
    if (auto wake = native_audio_wait_.lock()) wake->Notify();
  }
  static std::optional<std::pair<WaitResult, size_t>> WaitNativeAudio(
      const std::vector<PosixConditionBase*>&, bool, std::chrono::milliseconds,
      const std::shared_ptr<recomp::native::WaitSet>&);

'''
    source = replace_once(source, '  virtual bool Signal() = 0;\n', '  virtual bool Signal() = 0;\n\n' + methods)
    source = replace_once(source, '  std::condition_variable cond_;\n',
                          '  std::weak_ptr<recomp::native::WaitSet> native_audio_wait_;\n  std::condition_variable cond_;\n')
    source = replace_in(source, 'class PosixCondition<Event>', 'class PosixCondition<Semaphore>',
                          '    signal_ = true;\n    cond_.notify_all();',
                          '    signal_ = true;\n    NotifyNativeAudio();\n    cond_.notify_all();')
    source = replace_in(source, 'class PosixCondition<Semaphore>', 'class PosixCondition<Mutant>',
                          '    count_ += release_count;\n    cond_.notify_all();',
                          '    count_ += release_count;\n    NotifyNativeAudio();\n    cond_.notify_all();')
    source = replace_once(source, '    // If the callback is queued on the current thread, don\'t self-signal.',
                          '''    // This runs on the queuing thread, outside the signal handler.
    {
      auto lock = NativeAudioLock();
      if (lock.owns_lock()) NotifyNativeAudio();
    }

    // If the callback is queued on the current thread, don't self-signal.''')
    method = '''// Audio's eight counted client semaphores plus pause/shutdown event only.
// Other wait families (including wait-all, mutants and joins) keep their SDK path.
std::optional<std::pair<WaitResult, size_t>> PosixConditionBase::WaitNativeAudio(
    const std::vector<PosixConditionBase*>& conditions, bool is_alertable,
    std::chrono::milliseconds timeout,
    const std::shared_ptr<recomp::native::WaitSet>& wake) {
  if (conditions.size() != 9) return std::nullopt;
  for (size_t i = 0; i < 8; ++i)
    if (!dynamic_cast<PosixCondition<Semaphore>*>(conditions[i])) return std::nullopt;
  if (!dynamic_cast<PosixCondition<Event>*>(conditions[8])) return std::nullopt;
  std::array<PosixConditionBase*, 9> ordered;
  std::copy(conditions.begin(), conditions.end(), ordered.begin());
  std::sort(ordered.begin(), ordered.end(), std::less<PosixConditionBase*>{});
  if (std::adjacent_find(ordered.begin(), ordered.end()) != ordered.end()) return std::nullopt;
  for (auto *condition : ordered)
    if (!condition->SubscribeNativeAudio(wake)) return std::nullopt;
  if (is_alertable) {
    if (!current_thread_condition_) Thread::GetCurrentThread();
    if (!current_thread_condition_ || !current_thread_condition_->SubscribeNativeAudio(wake)) return std::nullopt;
  }
  ScopedAlertableState alertable(is_alertable);
  const auto deadline = ComputeAlertableDeadline(timeout);
  constexpr size_t kCallback = 9, kFailure = 10, kExpired = 11;
  const auto result = wake->WaitUntil([&]() -> std::optional<size_t> {
    // Preserve callback priority and the SDK's zero-time alertable behavior.
    if (is_alertable && DispatchCurrentThreadUserCallback()) return kCallback;
    if (is_alertable && HasAlertableTimeoutElapsed(deadline)) return kExpired;
    // A stable total lock order avoids opposite-order deadlocks. No native
    // notification mutex is held while acquiring any condition/callback lock.
    std::array<std::unique_lock<std::mutex>, 9> locks;
    for (size_t i = 0; i < ordered.size(); ++i) {
      locks[i] = ordered[i]->NativeAudioLock();
      if (!locks[i].owns_lock()) return kFailure;
    }
    // Original array order determines wait-any priority and consumption.
    for (size_t i = 0; i < conditions.size(); ++i) {
      if (!conditions[i]->signaled()) continue;
      conditions[i]->post_execution();
      return i;
    }
    return std::nullopt;
  }, deadline);
  static thread_local uint32_t report_count = 0;
  if ((++report_count & 1023) == 0) rr6::ReportNativeAudioWait(*wake);
  if (!result || *result == kExpired) return std::make_pair(WaitResult::kTimeout, size_t(0));
  if (*result == kCallback) return std::make_pair(WaitResult::kUserCallback, size_t(0));
  if (*result == kFailure) return std::make_pair(WaitResult::kFailed, size_t(0));
  return std::make_pair(WaitResult::kSuccess, *result);
}

'''
    anchor = 'std::pair<WaitResult, size_t> WaitMultiple(WaitHandle* wait_handles[], size_t wait_handle_count,'
    source = replace_once(source, anchor, method + anchor)
    source = replace_once(source, '  if (!is_alertable) {\n    return PosixConditionBase::WaitMultiple',
                          '''  if (!wait_all) {
    if (auto wake = rr6::CurrentNativeAudioWaitSet()) {
      if (auto result = PosixConditionBase::WaitNativeAudio(conditions, is_alertable, timeout, wake)) return *result;
    }
  }
  if (!is_alertable) {
    return PosixConditionBase::WaitMultiple''')
    return source


def prepare_audio(source):
    if hashlib.sha256(source.encode()).hexdigest() != '7e129af6ea359947d4cd148d59f92c26984666c422e8d616b80fda005ffe7a07':
        raise ValueError('Unsupported SDK audio revision; review client signals, callbacks, pause and shutdown')
    source = '#include "native_audio_wait.h"\n' + source
    return replace_once(source, 'void AudioSystem::WorkerThreadMain() {\n',
                         'void AudioSystem::WorkerThreadMain() {\n  rr6::NativeAudioWaitScope native_wait_scope(rr6::NativeAudioWaitEnabled());\n')


if __name__ == '__main__':
    try:
        sdk, out = map(Path, sys.argv[1:])
        threading = prepare_threading((sdk / 'src/core/threading_posix.cpp').read_text())
        audio = prepare_audio((sdk / 'src/audio/audio_system.cpp').read_text())
        out.mkdir(parents=True, exist_ok=True)
        (out / 'threading_posix.cpp').write_text(threading)
        (out / 'audio_system.cpp').write_text(audio)
    except (ValueError, OSError) as error:
        sys.exit(str(error))
