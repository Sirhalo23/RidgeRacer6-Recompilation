// Slope-scaled depth bias: a few possible values instead of a new one per draw
// (pipeline-cache growth on Direct3D 12).
//
// During a race the game draws about 13 things per frame (one shader pair,
// quads with a stencil test and no depth write) with a slope-scaled depth bias
// that it works out afresh for each of them: 0.68, 0.78, 0.83, ... 10.1 within
// one frame, and different again in the next. The SDK's Direct3D 12 backend
// bakes that number into the pipeline it builds for the draw and stores every
// pipeline it has ever built, to rebuild them all at the next start. Measured
// on Windows before this fix: about 770 new pipelines per second for the whole
// length of a race, none in the menus; the stored file reached 273,327
// pipelines, 273,202 of them this shader pair and different from each other in
// this one number only.
//
// The game sets the value through its D3D render-state function
//   sub_82255CD8  SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, float bits)
// which is the only code that writes the two scale registers
// (PA_SU_POLY_OFFSET_FRONT/BACK_SCALE, kept at device+0x2E90 and +0x2E98).
// Here the value is rounded before that function sees it: the sign, the
// exponent and the top three mantissa bits are kept, and the size is rounded
// up, so there are eight possible values per power of two and the result is
// never a smaller offset than the game asked for, at most 12.5% larger. For
// an offset whose job is to stop two surfaces flickering against each other
// that is not visible, and a whole race now needs a few dozen pipelines.
//
// The plain depth bias (D3DRS_DEPTHBIAS) is left alone: the stored pipelines
// show a single value for it.
//
// rr6_depth_bias_stats = true logs, every five seconds, how many times the
// state was set and how many different values were asked for and passed on.

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_set>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "generated/default/rr6_recomp_init.h"

REXCVAR_DEFINE_BOOL(rr6_quantize_depth_bias, true, "RR6",
                    "Round the slope-scaled depth bias the game sets to eight values per power of "
                    "two, so that Direct3D 12 does not build a new pipeline for every draw.");

REXCVAR_DEFINE_BOOL(rr6_depth_bias_stats, false, "RR6",
                    "Diagnostic: every five seconds, log how often the game set the slope-scaled "
                    "depth bias and how many different values it asked for and got.");

namespace {

// Keeps the sign, the exponent and the top three mantissa bits, rounding the
// magnitude up. Zero, infinities and NaN pass through unchanged.
uint32_t QuantizeFloatBits(uint32_t bits) {
  constexpr uint32_t kDroppedMask = (1u << (23 - 3)) - 1;
  uint32_t magnitude = bits & 0x7FFFFFFFu;
  if (magnitude == 0 || magnitude >= 0x7F800000u) {
    return bits;
  }
  // Adding the mask carries into the kept bits (and from there into the
  // exponent, which is the next power of two) unless the dropped bits are zero.
  magnitude = (magnitude + kDroppedMask) & ~kDroppedMask;
  if (magnitude >= 0x7F800000u) {
    magnitude = 0x7F700000u;  // the largest value on the grid, not infinity
  }
  return (bits & 0x80000000u) | magnitude;
}

struct Stats {
  std::mutex mutex;
  std::unordered_set<uint32_t> asked;   // different values the game asked for
  std::unordered_set<uint32_t> passed;  // different values passed on
  uint64_t calls = 0;
  int64_t last_report_ms = 0;
};

Stats& GetStats() {
  static Stats stats;
  return stats;
}

void Count(uint32_t asked, uint32_t passed) {
  Stats& stats = GetStats();
  std::lock_guard<std::mutex> lock(stats.mutex);
  ++stats.calls;
  if (stats.asked.size() < 2000000) stats.asked.insert(asked);
  stats.passed.insert(passed);
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  if (now - stats.last_report_ms >= 5000) {
    stats.last_report_ms = now;
    REXLOG_INFO("[depthbias] so far: slope-scaled depth bias set {} times, {} different values asked "
                "for, {} passed on",
                stats.calls, stats.asked.size(), stats.passed.size());
  }
}

}  // namespace

// SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS): r3 = device, r4 = the float's bits.
REX_HOOK_RAW(sub_82255CD8) {
  const uint32_t asked = ctx.r4.u32;
  const uint32_t passed = REXCVAR_GET(rr6_quantize_depth_bias) ? QuantizeFloatBits(asked) : asked;
  if (REXCVAR_GET(rr6_depth_bias_stats)) {
    Count(asked, passed);
  }
  ctx.r4.u64 = passed;
  __imp__sub_82255CD8(ctx, base);
}
