// Guest callbacks that the runtime invokes on its own host threads.
//
// The SDK seeds a thread's cached floating-point control word (ctx.fpscr.csr)
// only when a guest-created thread starts (XThread::Execute -> InitHost). The
// audio worker and the graphics interrupt thread are host threads, so their
// cached word stays 0. The first flush-mode toggle in recompiled code then
// writes that word to MXCSR, which UNMASKS every floating-point exception, and
// the next inexact result kills the process. Second Windows run died this way
// in the audio mixer (sub_822A8AE0) with STATUS_FLOAT_INEXACT_RESULT, right
// after the loading screen when the first sound started.
//
// Fix: seed the control word on entry to both callbacks, then run the original.

#include <rex/hook.h>

#include "generated/default/rr6_recomp_init.h"

// XAudio render-driver callback (registered via XAudioRegisterRenderDriverClient).
REX_HOOK_RAW(sub_822AA050) {
  ctx.fpscr.InitHost();
  __imp__sub_822AA050(ctx, base);
}

// Graphics interrupt callback (registered via VdSetGraphicsInterruptCallback).
REX_HOOK_RAW(sub_8225A590) {
  ctx.fpscr.InitHost();
  __imp__sub_8225A590(ctx, base);
}
