// How long the game waits for the graphics chip and for files (issue #7).
//
// Two places where the game can hold up a thread on a slow PC, measured so
// testers' logs say whether either matters before anything is changed:
//
//   Waiting for the graphics chip. sub_8225B828 hands the graphics chip a
//   marker and then reads a word of the device (+10492) over and over until
//   the chip has passed the marker: it waits until the chip is idle, using the
//   processor the whole time (on the console the chip was fast and this was
//   short). Here the "chip" is the SDK's command thread turning the game's
//   commands into Direct3D 12 or Vulkan work, so on a slow PC the wait can be
//   long and keeps a core busy doing nothing. It is called from eight places
//   in the game's graphics code; the log names the two that wait longest.
//
//   File reads. sub_822485F0 is the game's ReadFile; every read of the game's
//   data goes through it. The SDK reads files on the calling thread even when
//   the game asks for a read in the background, so a read holds up the thread
//   that asked for it.
//
// Every 30 seconds, after the [threads] line (thread_stats.cpp):
//
//   [waits] graphics chip: 1 waits, 0 ms in all (longest 0.3 ms; most from
//           8225BCDC 0 ms); file reads: 34, 38.3 MB in 55 ms (longest 5.8 ms)
//
// (the Linux test machine at the title screen; the callers are the return
// addresses of the waits that took longest)
//
// The timing costs two clock reads per call. rr6_wait_stats = false turns it
// off.

#include "wait_stats.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "generated/default/rr6_recomp_init.h"

REXCVAR_DEFINE_BOOL(rr6_wait_stats, true, "RR6",
                    "Every 30 seconds, log how long the game waited for the graphics chip and "
                    "for file reads (next to the [threads] line).");

namespace {

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct Tally {
  std::atomic<uint64_t> count{0};
  std::atomic<int64_t> total_ns{0};
  std::atomic<int64_t> longest_ns{0};
  std::atomic<uint64_t> bytes{0};

  void Add(int64_t ns, uint64_t size = 0) {
    count.fetch_add(1, std::memory_order_relaxed);
    total_ns.fetch_add(ns, std::memory_order_relaxed);
    bytes.fetch_add(size, std::memory_order_relaxed);
    int64_t longest = longest_ns.load(std::memory_order_relaxed);
    while (ns > longest &&
           !longest_ns.compare_exchange_weak(longest, ns, std::memory_order_relaxed)) {
    }
  }
};

Tally g_gpu_waits;
Tally g_reads;

// Time spent waiting for the graphics chip, by the address the wait was
// called from. Eight callers; a short list under a lock.
std::mutex g_callers_mutex;
std::vector<std::pair<uint32_t, int64_t>> g_callers;

void AddCaller(uint32_t caller, int64_t ns) {
  std::lock_guard<std::mutex> lock(g_callers_mutex);
  for (auto& entry : g_callers) {
    if (entry.first == caller) {
      entry.second += ns;
      return;
    }
  }
  g_callers.emplace_back(caller, ns);
}

double Ms(int64_t ns) { return double(ns) / 1e6; }

}  // namespace

// Wait until the graphics chip is idle (see the top of this file).
REX_HOOK_RAW(sub_8225B828) {
  if (!REXCVAR_GET(rr6_wait_stats)) {
    __imp__sub_8225B828(ctx, base);
    return;
  }
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const int64_t start = NowNs();
  __imp__sub_8225B828(ctx, base);
  const int64_t ns = NowNs() - start;
  g_gpu_waits.Add(ns);
  AddCaller(caller, ns);
}

// ReadFile(file, buffer, bytes to read, bytes read, overlapped)
REX_HOOK_RAW(sub_822485F0) {
  if (!REXCVAR_GET(rr6_wait_stats)) {
    __imp__sub_822485F0(ctx, base);
    return;
  }
  const uint32_t size = ctx.r5.u32;
  const int64_t start = NowNs();
  __imp__sub_822485F0(ctx, base);
  g_reads.Add(NowNs() - start, size);
}

namespace rr6 {

void LogWaitStats() {
  if (!REXCVAR_GET(rr6_wait_stats)) {
    return;
  }
  const uint64_t waits = g_gpu_waits.count.exchange(0);
  const int64_t wait_ns = g_gpu_waits.total_ns.exchange(0);
  const int64_t wait_longest = g_gpu_waits.longest_ns.exchange(0);
  const uint64_t reads = g_reads.count.exchange(0);
  const int64_t read_ns = g_reads.total_ns.exchange(0);
  const int64_t read_longest = g_reads.longest_ns.exchange(0);
  const uint64_t read_bytes = g_reads.bytes.exchange(0);

  std::vector<std::pair<uint32_t, int64_t>> callers;
  {
    std::lock_guard<std::mutex> lock(g_callers_mutex);
    callers.swap(g_callers);
  }
  std::sort(callers.begin(), callers.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  std::string most;
  for (size_t i = 0; i < callers.size() && i < 2; ++i) {
    most += fmt::format("{}{:08X} {:.0f} ms", i ? ", " : "; most from ", callers[i].first,
                        Ms(callers[i].second));
  }

  REXLOG_INFO(
      "[waits] graphics chip: {} waits, {:.0f} ms in all (longest {:.1f} ms{}); file reads: {}, "
      "{:.1f} MB in {:.0f} ms (longest {:.1f} ms)",
      waits, Ms(wait_ns), Ms(wait_longest), most, reads, double(read_bytes) / (1024.0 * 1024.0),
      Ms(read_ns), Ms(read_longest));
}

}  // namespace rr6
