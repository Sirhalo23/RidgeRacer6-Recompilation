#pragma once
// Transitional SDK adapter only. The reusable WaitSet has no runtime types.
#include <memory>
#include <recomp/native/wait_set.h>

namespace rr6 {
// One audio worker owns a notification domain. Semaphore/event counters remain
// authoritative in the SDK during migration. Observers keep only weak owners.
__attribute__((visibility("default"))) std::shared_ptr<recomp::native::WaitSet>
CurrentNativeAudioWaitSet();
__attribute__((visibility("default"))) bool NativeAudioWaitEnabled();
__attribute__((visibility("default"))) void
ReportNativeAudioWait(const recomp::native::WaitSet &);
class __attribute__((visibility("default"))) NativeAudioWaitScope {
public:
  explicit NativeAudioWaitScope(bool enabled);
  ~NativeAudioWaitScope();
  NativeAudioWaitScope(const NativeAudioWaitScope &) = delete;
  NativeAudioWaitScope &operator=(const NativeAudioWaitScope &) = delete;

private:
  std::shared_ptr<recomp::native::WaitSet> previous_, current_;
};
} // namespace rr6
