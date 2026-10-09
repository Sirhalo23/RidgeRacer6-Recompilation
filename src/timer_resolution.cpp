// Windows timer resolution (issue #7).
//
// The SDK paces the game with short sleeps: the thread that stands in for the
// console's vertical blank checks the time every millisecond (Sleep(1)), and
// other waits work the same way. On Windows a sleep lasts at least one tick of
// the system timer, and since Windows 10 version 2004 that tick is 15.6 ms for
// every program that does not ask for a finer one itself, whatever other
// programs ask for. The SDK does not ask (Xenia, which it comes from, does), so
// on such a PC "sleep 1 ms" sleeps 15.6 ms: the 60 Hz vertical blank arrives in
// uneven steps, and anything waiting on a short sleep runs late.
//
// The game program asks for a 1 ms timer when it starts (timeBeginPeriod), as
// games and emulators commonly do. Windows restores the default when the
// program ends. The log says how long a 1 ms sleep took before and after.
// Linux sleeps are precise already; nothing is done there.

#include "timer_resolution.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#if defined(_WIN32)
#include <chrono>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mmsystem.h>
#endif

REXCVAR_DEFINE_BOOL(rr6_fine_timer, true, "RR6",
                    "Windows: ask for a 1 ms system timer so short waits are short (they "
                    "otherwise last 15.6 ms on Windows 10 2004 and later).");

namespace rr6 {

#if defined(_WIN32)
namespace {

// The average length of a few 1 ms sleeps, in milliseconds.
double MeasureSleep() {
  using Clock = std::chrono::steady_clock;
  constexpr int kSleeps = 8;
  const auto start = Clock::now();
  for (int i = 0; i < kSleeps; ++i) {
    ::Sleep(1);
  }
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count() / kSleeps;
}

}  // namespace

void UseFineTimer() {
  const double before = MeasureSleep();
  if (!REXCVAR_GET(rr6_fine_timer)) {
    REXLOG_INFO("[timer] a 1 ms sleep takes {:.1f} ms (rr6_fine_timer is off)", before);
    return;
  }
  TIMECAPS caps{};
  UINT period = 1;
  if (timeGetDevCaps(&caps, sizeof(caps)) == MMSYSERR_NOERROR && caps.wPeriodMin > period) {
    period = caps.wPeriodMin;
  }
  const MMRESULT result = timeBeginPeriod(period);
  const double after = MeasureSleep();
  REXLOG_INFO("[timer] a 1 ms sleep took {:.1f} ms; asked for a {} ms timer ({}), now {:.1f} ms",
              before, period, result == TIMERR_NOERROR ? "granted" : "refused", after);
}

#else

void UseFineTimer() {}

#endif

}  // namespace rr6
