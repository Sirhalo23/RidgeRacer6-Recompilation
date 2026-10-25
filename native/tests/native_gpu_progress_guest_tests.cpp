#include "gpu_progress_fixture.h"
#include "native_gpu_progress.h"
#include <array>
#include <cstring>
#include <iostream>
namespace {
constexpr uint32_t kDevice = 0x1000, kWord = 0x9000, kThread = 0xA000,
                   kPcr = 0xB000, kStack = 0xF000;
bool native_enabled = true;
} // namespace
namespace rr6 {
bool GpuProgressEnabled() { return native_enabled; }
std::optional<ProgressPointers> ResolveGpuProgress(uint8_t *base,
                                                   uint32_t device) {
  if (device != kDevice)
    return std::nullopt;
  auto word = GpuProgressRead32(base, device + 10384);
  if (word != kWord)
    return std::nullopt;
  return ProgressPointers{base + device, base + word, base + word, word};
}
void ReportGpuProgress() {}
} // namespace rr6
#define DEFINE_PROGRESS_STUBS(ns)                                              \
  namespace ns {                                                               \
  rr6_test::Signals signals;                                                   \
  void __savegprlr_29(PPCContext &ctx, uint8_t *base) {                        \
    REX_STORE_U64(ctx.r1.u32 - 32, ctx.r29.u64);                               \
    REX_STORE_U64(ctx.r1.u32 - 24, ctx.r30.u64);                               \
    REX_STORE_U64(ctx.r1.u32 - 16, ctx.r31.u64);                               \
    REX_STORE_U32(ctx.r1.u32 - 8, ctx.r12.u32);                                \
  }                                                                            \
  void __restgprlr_29(PPCContext &ctx, uint8_t *base) {                        \
    ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 - 32);                               \
    ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 - 24);                               \
    ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 - 16);                               \
    ctx.lr = REX_LOAD_U32(ctx.r1.u32 - 8);                                     \
  }                                                                            \
  void sub_8231CBB8(PPCContext &ctx, uint8_t *base) {                          \
    auto thread = REX_LOAD_U32(ctx.r13.u32 + 256);                             \
    ctx.r3.u64 = REX_LOAD_U32(thread + 332);                                   \
    if (signals.on_thread_id)                                                  \
      signals.on_thread_id(ctx, base, ++signals.thread_id_calls);              \
    else                                                                       \
      ++signals.thread_id_calls;                                               \
  }                                                                            \
  void sub_8225B5E0(PPCContext &, uint8_t *) { ++signals.flushes; }            \
  void sub_82262840(PPCContext &, uint8_t *) { ++signals.logs; }               \
  void sub_82262B40(PPCContext &, uint8_t *) { ++signals.callbacks; }          \
  void sub_8225DD98(PPCContext &, uint8_t *) { ++signals.recovery; }           \
  }
DEFINE_PROGRESS_STUBS(original_progress)
DEFINE_PROGRESS_STUBS(native_progress)
namespace {
void Check(bool ok) {
  if (!ok)
    throw std::runtime_error("Original/native guest wait parity failed");
}
struct alignas(32) Arena {
  std::array<uint8_t, 0x11000> data{};
};
struct Result {
  PPCContext ctx{};
  Arena arena;
  std::array<unsigned, 5> calls{};
  int trap = 0;
};
Result Run(bool native, int scenario) {
  Result r;
  auto *base = r.arena.data.data();
  auto &ctx = r.ctx;
  ctx.r1.u64 = kStack;
  ctx.r13.u64 = kPcr;
  ctx.r3.u64 = kDevice;
  ctx.r4.u64 = 4;
  ctx.r6.u64 = 0;
  ctx.r29.u64 = 0x9876543212345678;
  ctx.r30.u64 = 0x1234987656781234;
  ctx.r31.u64 = 0xABCDEF0001020304;
  ctx.lr = 0x12345678;
  REX_STORE_U32(kDevice + 10384, kWord);
  REX_STORE_U32(kDevice + 10396, 6);
  REX_STORE_U32(kDevice + 10376, 7);
  REX_STORE_U32(kPcr + 256, kThread);
  REX_STORE_U32(kThread + 332, 7);
  auto &signals =
      native ? native_progress::signals : original_progress::signals;
  signals = {};
  if (scenario == 0)
    REX_STORE_U32(kWord, 4); // Already ready.
  if (scenario == 1)
    ctx.r4.u64 = 0; // Zero target exits without polling.
  if (scenario == 2)
    REX_STORE_U8(kDevice + 10433, 4); // Original helper abort flag.
  if (scenario == 3 || scenario == 4 || scenario == 5) {
    if (scenario != 5)
      REX_STORE_U32(kDevice + 13628, 1); // Recovery callback enabled/absent.
    signals.on_thread_id = [scenario](PPCContext &, uint8_t *base, unsigned n) {
      if (n == 2)
        rr6::GpuProgressStore32(base, kThread + 88, 2001);
      if (scenario == 4 && n == 2)
        rr6::GpuProgressStore32(base, kWord, 2); // Progress refreshes watchdog.
      if (scenario == 4 && n == 3)
        rr6::GpuProgressStore32(base, kWord, 4);
    };
  }
  if (scenario == 6) {
    ctx.r4.u64 = 0xFFFFFFFE;
    REX_STORE_U32(kDevice + 10396, 2);
    REX_STORE_U32(kWord, 0xFFFFFFFC);
    signals.on_thread_id = [](PPCContext &, uint8_t *base, unsigned n) {
      if (n == 2)
        rr6::GpuProgressStore32(base, kWord, 0xFFFFFFFE);
    };
  }
  if (scenario == 7) { // Preflush path and then producer progress.
    ctx.r4.u64 = 6;
    signals.on_thread_id = [](PPCContext &, uint8_t *base, unsigned n) {
      if (n == 2)
        rr6::GpuProgressStore32(base, kWord, 6);
    };
  }
  try {
    if (native)
      native_progress::sub_8225B038(ctx, base);
    else
      original_progress::sub_8225B038(ctx, base);
  } catch (const rr6_test::Trap &e) {
    r.trap = e.code;
  }
  r.calls = {signals.thread_id_calls, signals.flushes, signals.logs,
             signals.callbacks, signals.recovery};
  return r;
}
} // namespace
int main() {
  try {
    for (int scenario = 0; scenario < 8; ++scenario) {
      auto a = Run(false, scenario);
      auto b = Run(true, scenario);
      Check(a.arena.data == b.arena.data && a.calls == b.calls &&
            a.trap == b.trap);
      Check(a.ctx.r1.u64 == b.ctx.r1.u64 && a.ctx.r29.u64 == b.ctx.r29.u64 &&
            a.ctx.r30.u64 == b.ctx.r30.u64 && a.ctx.r31.u64 == b.ctx.r31.u64 &&
            a.ctx.lr == b.ctx.lr && a.ctx.r3.u64 == b.ctx.r3.u64);
      if (scenario == 3)
        Check(a.calls[3] == 1 && a.calls[4] == 1 && a.trap == 0);
      if (scenario == 4)
        Check(a.calls[4] == 0);
      if (scenario == 5)
        Check(a.trap == 22);
    }
    auto s = rr6::GetGpuProgressStatistics();
    Check(s.parks > 0 && s.timer_rechecks > 0 && s.recoveries == 2);
    std::cout
        << "Pinned original/native guest bodies: ready, zero, wrap, preflush, "
           "watchdog refresh, recovery callback and trap parity passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
