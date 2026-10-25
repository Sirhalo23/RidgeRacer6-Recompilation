#pragma once
// Transitional adapter ABI. Blocking uses the SDK-free native WaitSet.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace rr6 {
#if defined(__GNUC__) || defined(__clang__)
#define RR6_PROGRESS_API __attribute__((visibility("default")))
#else
#define RR6_PROGRESS_API
#endif
// Match the generated runtime's physical E-heap mapping on these hosts.
inline uint8_t *GpuProgressHost(uint8_t *base, uint32_t address) {
#if defined(_WIN32) ||                                                         \
    (defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__)))
  return base + address + (address >= 0xE0000000u ? 0x1000 : 0);
#else
  return base + address;
#endif
}
enum class ProgressWriter {
  Cpu,
  Event,
  Memory,
  Register,
  Conditional,
  Scratch,
  ReadPointer,
  Other,
  Count
};
struct ProgressPointers {
  uint8_t *device = nullptr, *word = nullptr, *physical_word = nullptr;
  uint32_t word_address = 0;
};
struct ProgressStatistics {
  uint64_t scopes = 0, waits = 0, probes = 0, parks = 0, timer_rechecks = 0,
           cancelled = 0, changed_storage = 0, recoveries = 0;
  std::array<uint64_t, size_t(ProgressWriter::Count)> writes{};
};
RR6_PROGRESS_API bool GpuProgressEnabled();
RR6_PROGRESS_API std::optional<ProgressPointers> ResolveGpuProgress(uint8_t *,
                                                                    uint32_t);
RR6_PROGRESS_API void ReportGpuProgress();
RR6_PROGRESS_API ProgressStatistics GetGpuProgressStatistics();
RR6_PROGRESS_API uint32_t GpuProgressRead32(const uint8_t *, uint32_t);
RR6_PROGRESS_API void GpuProgressStore32(uint8_t *, uint32_t, uint32_t);
RR6_PROGRESS_API void GpuProgressStore8(uint8_t *, uint32_t, uint8_t);
RR6_PROGRESS_API void InvalidateGpuProgressDevice(uint8_t *, uint32_t);
RR6_PROGRESS_API void InvalidateAllGpuProgress();
RR6_PROGRESS_API void GpuProgressRecovery();
// Same unsigned-distance comparison as RR6's original wait, including wrap.
constexpr bool GpuProgressPending(uint32_t submitted, uint32_t target,
                                  uint32_t completed) {
  return uint32_t(submitted - target) < uint32_t(submitted - completed);
}
class RR6_PROGRESS_API GpuProgressWrite {
public:
  GpuProgressWrite(void *, size_t, ProgressWriter);
  ~GpuProgressWrite();
  GpuProgressWrite(const GpuProgressWrite &) = delete;
  GpuProgressWrite &operator=(const GpuProgressWrite &) = delete;

private:
  std::unique_lock<std::mutex> lock_;
  uintptr_t address_;
  size_t size_;
  ProgressWriter writer_;
};
class RR6_PROGRESS_API GpuProgressScope {
public:
  struct Impl;
  GpuProgressScope(uint8_t *base, uint32_t device);
  ~GpuProgressScope();
  GpuProgressScope(const GpuProgressScope &) = delete;
  GpuProgressScope &operator=(const GpuProgressScope &) = delete;
  // Called only at the original B038 polling site. It never reports a fence as
  // complete or changes guest registers. The original helper runs afterward.
  void Park(uint32_t target, uint32_t previous_progress);

private:
  std::shared_ptr<Impl> impl_;
};
#undef RR6_PROGRESS_API
} // namespace rr6
