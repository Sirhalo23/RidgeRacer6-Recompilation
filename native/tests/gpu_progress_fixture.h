#pragma once
#include "rr6_recomp_pch.h"
#include <functional>
#include <stdexcept>
namespace rr6_test {
struct Trap : std::runtime_error {
  int code;
  explicit Trap(int n) : std::runtime_error("guest trap"), code(n) {}
};
struct Signals {
  unsigned thread_id_calls = 0, flushes = 0, logs = 0, callbacks = 0,
           recovery = 0;
  std::function<void(PPCContext &, uint8_t *, unsigned)> on_thread_id;
};
} // namespace rr6_test
#undef DEFINE_REX_FUNC
#define DEFINE_REX_FUNC(name) void name(PPCContext &ctx, uint8_t *base)
#define ppc_trap(ctx, base, n) throw rr6_test::Trap(n)
#define DECLARE_PROGRESS_FIXTURE(ns)                                           \
  namespace ns {                                                               \
  extern rr6_test::Signals signals;                                            \
  void sub_8225B038(PPCContext &, uint8_t *);                                  \
  void sub_8225AFB0(PPCContext &, uint8_t *);                                  \
  void sub_82262C00(PPCContext &, uint8_t *);                                  \
  void sub_82262C20(PPCContext &, uint8_t *);                                  \
  void sub_8231CBB8(PPCContext &, uint8_t *);                                  \
  void sub_8225B5E0(PPCContext &, uint8_t *);                                  \
  void __savegprlr_29(PPCContext &, uint8_t *);                                \
  void __restgprlr_29(PPCContext &, uint8_t *);                                \
  void sub_82262840(PPCContext &, uint8_t *);                                  \
  void sub_82262B40(PPCContext &, uint8_t *);                                  \
  void sub_8225DD98(PPCContext &, uint8_t *);                                  \
  }
DECLARE_PROGRESS_FIXTURE(original_progress)
DECLARE_PROGRESS_FIXTURE(native_progress)
