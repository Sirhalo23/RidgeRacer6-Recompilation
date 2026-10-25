#include "native_audio_wait.h"
#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(
    rr6_native_audio_wait, true, "RR6",
    "Use native notification-driven audio waits (when built in)");
namespace rr6 {
namespace {
thread_local std::shared_ptr<recomp::native::WaitSet> current;
}
std::shared_ptr<recomp::native::WaitSet> CurrentNativeAudioWaitSet() {
  return current;
}
bool NativeAudioWaitEnabled() { return REXCVAR_GET(rr6_native_audio_wait); }
void ReportNativeAudioWait(const recomp::native::WaitSet &set) {
  const auto s = set.GetStatistics();
  REXLOG_INFO(
      "[native-audio] waits={} probes={} parks={} notifications={} timeouts={}",
      s.waits, s.probes, s.parks, s.notifications, s.timeouts);
}
NativeAudioWaitScope::NativeAudioWaitScope(bool enabled) : previous_(current) {
  if (enabled)
    current_ = std::make_shared<recomp::native::WaitSet>();
  current = current_;
  REXLOG_INFO("[native-audio] worker route={}",
              enabled ? "native" : "original");
}
NativeAudioWaitScope::~NativeAudioWaitScope() {
  if (current_)
    ReportNativeAudioWait(*current_);
  current = std::move(previous_);
}
} // namespace rr6
