// Test-rig only (not part of the Windows build): counts the different
// slope-scale values that are in the device's register copy at the moment the
// graphics library sends the polygon-offset registers to the GPU.
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_set>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "generated/default/rr6_recomp_init.h"

namespace {
std::mutex g_mutex;
std::unordered_set<uint32_t> g_values;
uint64_t g_flushes = 0, g_dirty = 0;
int64_t g_last = 0;
float g_min = 1e30f, g_max = -1e30f;

void Probe(PPCContext& ctx, uint8_t* base) {
  const uint32_t device = ctx.r3.u32;
  const uint64_t mask = REX_LOAD_U64(device + 48);
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_flushes;
  if (mask & 0x03FC0000ull) {
    ++g_dirty;
    const uint32_t bits = REX_LOAD_U32(device + 0x2E90);
    g_values.insert(bits);
    float f;
    memcpy(&f, &bits, 4);
    if (f != 0.0f) {
      if (f < g_min) g_min = f;
      if (f > g_max) g_max = f;
    }
  }
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch()).count();
  if (now - g_last >= 5000) {
    g_last = now;
    REXLOG_INFO("[rigprobe] state flushes {}, with polygon-offset registers dirty {}, different front-scale register values sent {} (non-zero range {} .. {})",
                g_flushes, g_dirty, g_values.size(), g_min, g_max);
  }
}
}  // namespace

REX_HOOK_RAW(sub_8225DA08) { Probe(ctx, base); __imp__sub_8225DA08(ctx, base); }
REX_HOOK_RAW(sub_8225D278) { Probe(ctx, base); __imp__sub_8225D278(ctx, base); }
