// The game's frame rate, for the SDK's F3 window and for the log.
//
// The SDK's F3 window shows "Guest: N FPS (M ms)" only when the program
// supplies those numbers (ReXApp::SetGuestFrameStats); without that it is an
// empty box. The game ends every frame in its swap routine, sub_82259A28,
// which hands the finished frame to the kernel (VdSwap), so frames are counted
// there.
//
// The log gets one line every 30 seconds: the average frame rate and the
// slowest frame in that time. Bug reports then say how fast the game ran on
// the player's machine without anyone having to read numbers off the screen.

#include "frame_stats.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "generated/default/rr6_recomp_init.h"

namespace {

using Clock = std::chrono::steady_clock;

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
      .count();
}

std::atomic<uint64_t> g_frames{0};
std::atomic<int64_t> g_last_frame_ns{0};
// For the log: frames and the longest gap since the last log line.
std::atomic<int64_t> g_window_start_ns{0};
std::atomic<uint64_t> g_window_frames{0};
std::atomic<int64_t> g_window_longest_ns{0};

constexpr int64_t kLogEveryNs = 30'000'000'000;

void CountFrame() {
  const int64_t now = NowNs();
  const int64_t previous = g_last_frame_ns.exchange(now, std::memory_order_relaxed);
  g_frames.fetch_add(1, std::memory_order_relaxed);
  if (previous == 0) {
    g_window_start_ns.store(now, std::memory_order_relaxed);
    return;
  }
  const int64_t gap = now - previous;
  int64_t longest = g_window_longest_ns.load(std::memory_order_relaxed);
  while (gap > longest &&
         !g_window_longest_ns.compare_exchange_weak(longest, gap, std::memory_order_relaxed)) {
  }
  const uint64_t frames = g_window_frames.fetch_add(1, std::memory_order_relaxed) + 1;
  int64_t start = g_window_start_ns.load(std::memory_order_relaxed);
  if (now - start >= kLogEveryNs &&
      g_window_start_ns.compare_exchange_strong(start, now, std::memory_order_relaxed)) {
    const double seconds = double(now - start) / 1e9;
    const double slowest_ms = double(g_window_longest_ns.exchange(0)) / 1e6;
    g_window_frames.store(0, std::memory_order_relaxed);
    REXLOG_INFO("[fps] {:.1f} frames per second over the last {:.0f} s, slowest frame {:.1f} ms",
                double(frames) / seconds, seconds, slowest_ms);
  }
}

}  // namespace

// The game's swap routine: count the frame, then let it run as before.
REX_HOOK_RAW(sub_82259A28) {
  CountFrame();
  __imp__sub_82259A28(ctx, base);
}

namespace rr6 {

rex::ui::DebugOverlayDialog::FrameStatsProvider FrameStatsProvider() {
  // Called by the F3 window on the UI thread: it measures over half a second
  // at a time so the number stays readable.
  struct Sample {
    uint64_t frames = 0;
    int64_t time_ns = 0;
    double fps = 0;
    double frame_ms = 0;
  };
  auto sample = std::make_shared<Sample>();
  return [sample]() {
    rex::ui::FrameStats stats;
    const uint64_t frames = g_frames.load(std::memory_order_relaxed);
    const int64_t now = NowNs();
    if (sample->time_ns == 0) {
      sample->frames = frames;
      sample->time_ns = now;
    } else if (now - sample->time_ns >= 500'000'000) {
      const uint64_t done = frames - sample->frames;
      const double seconds = double(now - sample->time_ns) / 1e9;
      sample->fps = double(done) / seconds;
      sample->frame_ms = done ? seconds * 1000.0 / double(done) : 0.0;
      sample->frames = frames;
      sample->time_ns = now;
    }
    stats.frame_count = frames;
    stats.fps = sample->fps;
    stats.frame_time_ms = sample->frame_ms;
    return stats;
  };
}

}  // namespace rr6
