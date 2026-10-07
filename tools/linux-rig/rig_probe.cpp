// Test-rig only (not part of the Windows build): counts the different
// slope-scale values that are in the device's register copy at the moment the
// graphics library sends the polygon-offset registers to the GPU.
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_set>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/achievements.h>

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

// Rig only: unlocks an achievement some seconds after start, to see the pop-up
// and what gets saved without having to earn one at one frame a second.
REXCVAR_DEFINE_INT32(rig_unlock_achievement, 0, "RR6", "Rig: unlock this achievement after rig_unlock_after seconds.");
REXCVAR_DEFINE_INT32(rig_unlock_after, 40, "RR6", "Rig: seconds before rig_unlock_achievement.");
static void MaybeUnlock() {
  static const auto start = std::chrono::steady_clock::now();
  static bool done = false;
  if (!done && REXCVAR_GET(rig_unlock_achievement) > 0 &&
      std::chrono::steady_clock::now() - start > std::chrono::seconds(REXCVAR_GET(rig_unlock_after))) {
    done = true;
    const bool first = rex::system::UnlockAchievement(static_cast<uint32_t>(REXCVAR_GET(rig_unlock_achievement)));
    REXLOG_INFO("[rigprobe] unlock achievement {}: first time {}", REXCVAR_GET(rig_unlock_achievement), first);
  }
}

// Rig only: goes through the game's own way of awarding achievement 1 ("360!")
// without driving: marks it earned in the game's achievement manager
// (sub_82129818), runs the game's "write what was earned" step (sub_82129868,
// normally reached in the sequence after a race) and then its completion poll
// (sub_82129960). Shows whether the game's request reaches the SDK.
REXCVAR_DEFINE_INT32(rig_game_award_after, 0, "RR6", "Rig: seconds before the game is made to award achievement 1 through its own code (0 = off).");
static void MaybeGameAward(PPCContext& ctx, uint8_t* base) {
  static const auto start = std::chrono::steady_clock::now();
  static int stage = 0;
  static std::chrono::steady_clock::time_point at;
  constexpr uint32_t kManager = 0x824939A0;
  const int after = REXCVAR_GET(rig_game_award_after);
  if (after <= 0) return;
  const auto now = std::chrono::steady_clock::now();
  if (stage == 0 && now - start > std::chrono::seconds(after)) {
    stage = 1;
    at = now;
    const int8_t user = static_cast<int8_t>(REX_LOAD_U8(0x823B0C2C + 8));
    const uint32_t remap = REX_LOAD_U32(0x823B0C2C + 148);
    REXLOG_INFO("[rigprobe] game award: user index byte {}, remap flag {}, record 0: earned {} written {}", int(user), remap,
                REX_LOAD_U8(kManager + 16), REX_LOAD_U8(kManager + 17));
    PPCContext c = ctx;
    c.r3.u64 = kManager;
    c.r4.u64 = 0;
    sub_82129818(c, base);
    REXLOG_INFO("[rigprobe] game award: after mark, record 0: earned {} written {}", REX_LOAD_U8(kManager + 16), REX_LOAD_U8(kManager + 17));
    c = ctx;
    c.r3.u64 = kManager;
    sub_82129868(c, base);
    REXLOG_INFO("[rigprobe] game award: the game's write step returned {} (achievements it tried to write); request state {}", c.r3.u32,
                REX_LOAD_U32(0x82493F40 + 40));
  } else if (stage >= 1 && stage < 6 && now - at > std::chrono::seconds(2 * stage)) {
    ++stage;
    PPCContext c = ctx;
    c.r3.u64 = kManager;
    sub_82129960(c, base);
    REXLOG_INFO("[rigprobe] game award: completion poll returned {}; request state {}, result {:#x}", c.r3.u32, REX_LOAD_U32(0x82493F40 + 40),
                REX_LOAD_U32(0x82493F40 + 36));
  }
}

REX_HOOK_RAW(sub_8225DA08) { MaybeUnlock(); MaybeGameAward(ctx, base); Probe(ctx, base); __imp__sub_8225DA08(ctx, base); }
REX_HOOK_RAW(sub_8225D278) { Probe(ctx, base); __imp__sub_8225D278(ctx, base); }
