// Music that loops: XMASetLoopData (issue #17).
//
// The game plays its music as XMA audio, streamed in blocks. For a track that
// repeats, it tells the console's audio decoder where the loop starts and ends
// with XMASetLoopData(context, &loop_data), passing a 12-byte XMA_LOOP_DATA:
//
//   +0  u32  loop start   (bit offset of the first frame of the loop)
//   +4  u32  loop end     (bit offset of the last frame)
//   +8  u8   loop count   (255 = for ever)
//   +9  u8   loop subframe end
//   +10 u8   loop subframe skip
//
// The SDK's XMASetLoopData (v0.10.0, as in Xenia) reads that pointer as if it
// were a whole XMA context: the fields come from the wrong bytes, in the wrong
// byte order, and the loop start and end from past the end of the 12 bytes.
// The decoder then loops at a random place or not at all, so the menu music
// stopped after its first pass (issue #17). XMAInitializeContext reads its
// loop data correctly, so a sound set up in one go was not affected.
//
// The game program answers XMASetLoopData itself (as language.cpp does for
// XGetLanguage): it reads the 12 bytes as they are laid out and writes only the
// loop fields of the context, each 32-bit word with one atomic update, so the
// decoder's own changes to the same words (buffer valid flags, read offsets)
// made at the same moment are not undone.

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>

#include <rex/audio/xma/context.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(rr6_xma_loop_fix, true, "RR6",
                    "Read the game's XMA loop settings correctly, so music loops (issue #17)");

namespace {

using rex::audio::XMA_CONTEXT_DATA;

constexpr int kContextWords = sizeof(XMA_CONTEXT_DATA) / 4;

// Which bits of each context word hold the loop settings.
struct LoopMask {
  std::array<uint32_t, kContextWords> words = {};
  LoopMask() {
    uint8_t zero[sizeof(XMA_CONTEXT_DATA)] = {};
    XMA_CONTEXT_DATA ones(zero);
    ones.loop_count = 0xFF;
    ones.loop_subframe_end = 0x3;
    ones.loop_subframe_skip = 0x7;
    ones.loop_start = 0x3FFFFFF;
    ones.loop_end = 0x3FFFFFF;
    words = std::bit_cast<std::array<uint32_t, kContextWords>>(ones);
  }
};

std::atomic<int> g_logged{0};

}  // namespace

extern "C" REX_FUNC(__imp__XMASetLoopData) {
  (void)base;
  const uint32_t context_address = ctx.r3.u32;
  const uint32_t loop_address = ctx.r4.u32;
  ctx.r3.u64 = 0;
  if (!context_address || !loop_address) {
    return;
  }
  auto* memory = REX_KERNEL_MEMORY();
  auto* context = memory->TranslateVirtual<uint8_t*>(context_address);
  const auto* loop = memory->TranslateVirtual<const uint8_t*>(loop_address);

  const uint32_t loop_start = rex::memory::load_and_swap<uint32_t>(loop + 0);
  const uint32_t loop_end = rex::memory::load_and_swap<uint32_t>(loop + 4);
  const uint8_t loop_count = loop[8];
  const uint8_t loop_subframe_end = loop[9];
  const uint8_t loop_subframe_skip = loop[10];

  if (!REXCVAR_GET(rr6_xma_loop_fix)) {
    // The SDK's reading, kept for comparison: the loop data taken as a context.
    XMA_CONTEXT_DATA data(context);
    const auto* wrong = reinterpret_cast<const XMA_CONTEXT_DATA*>(loop);
    data.loop_start = wrong->loop_start;
    data.loop_end = wrong->loop_end;
    data.loop_count = wrong->loop_count;
    data.loop_subframe_end = wrong->loop_subframe_end;
    data.loop_subframe_skip = wrong->loop_subframe_skip;
    data.Store(context);
    return;
  }

  // The new values in the context's layout (host byte order).
  uint8_t zero[sizeof(XMA_CONTEXT_DATA)] = {};
  XMA_CONTEXT_DATA wanted(zero);
  wanted.loop_start = loop_start;
  wanted.loop_end = loop_end;
  wanted.loop_count = loop_count;
  wanted.loop_subframe_end = loop_subframe_end;
  wanted.loop_subframe_skip = loop_subframe_skip;
  const auto wanted_words = std::bit_cast<std::array<uint32_t, kContextWords>>(wanted);

  static const LoopMask mask;
  for (int i = 0; i < kContextWords; ++i) {
    if (!mask.words[i]) {
      continue;
    }
    std::atomic_ref<uint32_t> word(*reinterpret_cast<uint32_t*>(context + i * 4));
    uint32_t old_be = word.load(std::memory_order_relaxed);
    uint32_t new_be;
    do {
      const uint32_t old_host = rex::byte_swap(old_be);
      const uint32_t new_host = (old_host & ~mask.words[i]) | (wanted_words[i] & mask.words[i]);
      new_be = rex::byte_swap(new_host);
    } while (!word.compare_exchange_weak(old_be, new_be, std::memory_order_acq_rel));
  }

  if (g_logged.fetch_add(1) < 4) {
    // Once or twice per sound is plenty: enough to see the loop in a bug report.
    const auto* wrong = reinterpret_cast<const XMA_CONTEXT_DATA*>(loop);
    REXLOG_INFO(
        "[xma] loop for context {:08X}: start {} end {} count {} subframe end {} skip {} "
        "(the SDK would have read start {} end {} count {})",
        context_address, loop_start, loop_end, loop_count, loop_subframe_end, loop_subframe_skip,
        uint32_t(wrong->loop_start), uint32_t(wrong->loop_end), uint32_t(wrong->loop_count));
  }
}
