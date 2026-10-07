// Controller input as the game reads it: our overlays (overlay_input.cpp) and
// a diagnostic.
//
// rr6_log_input = true writes a line to the log every time the controller
// state the game reads for player 1 changes (buttons, triggers, left stick).
// Keyboard keys arrive the same way once mnk_mode has turned them into
// controller buttons. Useful when a tester reports "my controller / keyboard
// does nothing": the log shows whether the game received the input at all.
//
// Button bits (XINPUT): 0x0001 up, 0x0002 down, 0x0004 left, 0x0008 right,
// 0x0010 Start, 0x0020 Back, 0x0040 left stick click, 0x0080 right stick click,
// 0x0100 LB, 0x0200 RB, 0x1000 A, 0x2000 B, 0x4000 X, 0x8000 Y.
// Triggers go from 0 (released) to 255 (fully pressed).

#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include "generated/default/rr6_recomp_init.h"
#include "overlay_input.h"

REXCVAR_DEFINE_BOOL(rr6_log_input, false, "RR6",
                    "Diagnostic: log every change of the controller state the game reads for "
                    "player 1.");

// XInputGetState(user, XINPUT_STATE*): the game's only way of reading a pad.
// XINPUT_STATE: packet number (u32), buttons (u16), triggers (2 x u8), sticks.
REX_HOOK_RAW(sub_8224C9C0) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t state = ctx.r4.u32;
  __imp__sub_8224C9C0(ctx, base);
  // While one of our overlays is on screen the game gets an idle controller:
  // no buttons, triggers released, sticks centred.
  if (state && ctx.r3.u32 == 0 &&
      rr6::OverlaySeesPad(user, REX_LOAD_U16(state + 4),
                          static_cast<int16_t>(REX_LOAD_U16(state + 8)),
                          static_cast<int16_t>(REX_LOAD_U16(state + 10)))) {
    std::memset(base + state + 4, 0, 12);
  }
  if (!REXCVAR_GET(rr6_log_input) || user != 0 || !state) {
    return;
  }
  static std::atomic<uint64_t> last{~0ull};
  const uint32_t result = ctx.r3.u32;
  const uint32_t buttons = REX_LOAD_U16(state + 4);
  const uint32_t triggers = REX_LOAD_U16(state + 6);  // left trigger, then right trigger
  const int32_t lx = static_cast<int16_t>(REX_LOAD_U16(state + 8));
  const int32_t ly = static_cast<int16_t>(REX_LOAD_U16(state + 10));
  const uint64_t key = (uint64_t(result & 0xFFFF) << 48) | (uint64_t(buttons) << 32) |
                       (uint64_t(triggers) << 16) |
                       (uint64_t(lx > 16000 ? 1 : lx < -16000 ? 2 : 0) << 4) |
                       uint64_t(ly > 16000 ? 1 : ly < -16000 ? 2 : 0);
  if (last.exchange(key) != key) {
    REXLOG_INFO("[input] pad 1: result {:#x} buttons {:#06x} LT {} RT {} stick {},{}", result,
                buttons, triggers >> 8, triggers & 0xFF, lx, ly);
  }
}
