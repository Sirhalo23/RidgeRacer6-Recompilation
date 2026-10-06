// Safety net for the problem described in host_thread_fixes.cpp.
//
// If any other path reaches recompiled code with floating-point exceptions
// unmasked, this handler masks them again and resumes instead of letting the
// process die. Each hit is recorded in logs\fp-guard.txt (first few only) so
// the real source can be found and fixed properly.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>

namespace {

std::atomic<unsigned> g_hits{0};

LONG CALLBACK FpGuard(EXCEPTION_POINTERS* info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  const bool is_fp = (code >= 0xC000008Du && code <= 0xC0000093u) ||  // STATUS_FLOAT_*
                     code == 0xC00002B4u || code == 0xC00002B5u;      // multiple faults/traps
  if (!is_fp) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  CONTEXT* c = info->ContextRecord;
  const DWORD kMaskAll = 0x1F80u;  // IM|DM|ZM|OM|UM|PM
  if ((c->MxCsr & kMaskAll) == kMaskAll) {
    return EXCEPTION_CONTINUE_SEARCH;  // exceptions already masked: not this problem
  }
  const DWORD before = c->MxCsr;
  c->MxCsr = (c->MxCsr | kMaskAll) & ~0x3Fu;  // mask all, clear sticky flags
  c->FltSave.MxCsr = c->MxCsr;

  const unsigned n = g_hits.fetch_add(1);
  if (n < 16) {
    HMODULE exe = GetModuleHandleW(nullptr);
    const unsigned long long addr =
        reinterpret_cast<unsigned long long>(info->ExceptionRecord->ExceptionAddress);
    const unsigned long long base = reinterpret_cast<unsigned long long>(exe);
    if (FILE* f = std::fopen("logs\\fp-guard.txt", "a")) {
      std::fprintf(f, "hit %u: code %08lX at exe+%llX thread %lu mxcsr %08lX\n", n + 1,
                   static_cast<unsigned long>(code), addr - base,
                   static_cast<unsigned long>(GetCurrentThreadId()),
                   static_cast<unsigned long>(before));
      std::fclose(f);
    }
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}

struct Installer {
  Installer() { AddVectoredExceptionHandler(1, FpGuard); }
} g_installer;

}  // namespace

#endif  // _WIN32
