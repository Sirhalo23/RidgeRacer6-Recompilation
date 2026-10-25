#include "native_gpu_progress.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <recomp/native/wait_set.h>
#include <vector>
namespace rr6 {
namespace {
uint32_t RawRead(const uint8_t *p) {
  uint32_t v;
  std::copy_n(p, sizeof(v), reinterpret_cast<uint8_t *>(&v));
  if constexpr (std::endian::native == std::endian::little)
    v = __builtin_bswap32(v);
  return v;
}
struct Range {
  uintptr_t address;
  size_t size;
};
bool Overlap(uintptr_t a, size_t n, uintptr_t b, size_t m) {
  return n && m && (a <= b ? b - a < n : a - b < m);
}
struct Registry {
  std::mutex mutex;
  std::vector<std::weak_ptr<GpuProgressScope::Impl>> watches;
  ProgressStatistics statistics;
};
// SDK shutdown may hard-exit while host workers still exist. No static
// teardown.
Registry &State() {
  static auto *state = new Registry;
  return *state;
}
} // namespace
struct GpuProgressScope::Impl {
  ProgressPointers pointers;
  std::array<Range, 7> ranges;
  recomp::native::WaitSet wake;
  uint64_t generation = 0;
  bool cancelled = false, detached = false;
};
GpuProgressWrite::GpuProgressWrite(void *p, size_t size, ProgressWriter writer)
    : lock_(State().mutex), address_(reinterpret_cast<uintptr_t>(p)),
      size_(size), writer_(writer) {}
GpuProgressWrite::~GpuProgressWrite() {
  auto &state = State();
  for (auto &weak : state.watches) {
    auto watch = weak.lock();
    if (!watch || watch->cancelled || watch->detached)
      continue;
    const bool hit = std::any_of(
        watch->ranges.begin(), watch->ranges.end(), [&](const Range &r) {
          return Overlap(address_, size_, r.address, r.size);
        });
    if (hit) {
      ++watch->generation;
      ++state.statistics.writes[size_t(writer_)];
      watch->wake.Notify();
    }
  }
}
uint32_t GpuProgressRead32(const uint8_t *base, uint32_t address) {
  auto *host = GpuProgressHost(const_cast<uint8_t *>(base), address);
  if (!GpuProgressEnabled())
    return RawRead(host);
  std::lock_guard lock(State().mutex);
  return RawRead(host);
}
void GpuProgressStore32(uint8_t *base, uint32_t address, uint32_t value) {
  auto *host = GpuProgressHost(base, address);
  GpuProgressWrite write(host, 4, ProgressWriter::Cpu);
  if constexpr (std::endian::native == std::endian::little)
    value = __builtin_bswap32(value);
  std::copy_n(reinterpret_cast<const uint8_t *>(&value), sizeof(value), host);
}
void GpuProgressStore8(uint8_t *base, uint32_t address, uint8_t value) {
  auto *host = GpuProgressHost(base, address);
  GpuProgressWrite write(host, 1, ProgressWriter::Cpu);
  *host = value;
}
GpuProgressScope::GpuProgressScope(uint8_t *base, uint32_t device) {
  if (!GpuProgressEnabled())
    return;
  auto pointers = ResolveGpuProgress(base, device);
  if (!pointers || !pointers->device || !pointers->word ||
      !pointers->physical_word)
    return;
  auto &state = State();
  std::lock_guard lock(state.mutex);
  std::erase_if(state.watches, [](const auto &p) { return p.expired(); });
  if (state.watches.size() >= 64 ||
      RawRead(pointers->device + 10384) != pointers->word_address)
    return;
  auto watch = std::make_shared<Impl>();
  watch->pointers = *pointers;
  const auto dev = reinterpret_cast<uintptr_t>(pointers->device);
  watch->ranges = {{{reinterpret_cast<uintptr_t>(pointers->word), 4},
                    {reinterpret_cast<uintptr_t>(pointers->physical_word), 4},
                    {dev + 10384, 4},
                    {dev + 10396, 4},
                    {dev + 10433, 1},
                    {dev + 10376, 4},
                    {dev + 10488, 4}}};
  state.watches.push_back(watch);
  impl_ = std::move(watch);
  ++state.statistics.scopes;
}
GpuProgressScope::~GpuProgressScope() {
  if (!impl_)
    return;
  auto stats = impl_->wake.GetStatistics();
  auto &state = State();
  bool report;
  {
    std::lock_guard lock(state.mutex);
    state.statistics.waits += stats.waits;
    state.statistics.probes += stats.probes;
    state.statistics.parks += stats.parks;
    state.statistics.timer_rechecks += stats.timeouts;
    report = (state.statistics.scopes % 256) == 0;
  }
  impl_.reset();
  if (report)
    ReportGpuProgress();
}
void GpuProgressScope::Park(uint32_t target, uint32_t previous_progress) {
  auto watch = impl_;
  if (!watch)
    return;
  auto &state = State();
  uint64_t generation;
  {
    std::lock_guard lock(state.mutex);
    if (watch->cancelled || watch->detached)
      return;
    generation = watch->generation;
  }
  // The guest watchdog field is not a proven host clock. Retain its helper at
  // bounded intervals even if no GPU write arrives. Normal writes wake
  // directly.
  watch->wake.WaitUntil(
      [&]() -> std::optional<size_t> {
        std::lock_guard lock(state.mutex);
        if (watch->cancelled || watch->detached)
          return 0;
        auto &p = watch->pointers;
        if (RawRead(p.device + 10384) != p.word_address) {
          watch->detached = true;
          ++state.statistics.changed_storage;
          return 0;
        }
        const auto completed = RawRead(p.word);
        const auto submitted = RawRead(p.device + 10396);
        if (!GpuProgressPending(submitted, target, completed) ||
            completed != previous_progress || (p.device[10433] & 4) ||
            watch->generation != generation)
          return 0;
        return std::nullopt;
      },
      recomp::native::WaitSet::Clock::now() + std::chrono::milliseconds(1));
}
void InvalidateGpuProgressDevice(uint8_t *base, uint32_t device) {
  auto &state = State();
  std::lock_guard lock(state.mutex);
  for (auto &weak : state.watches)
    if (auto watch = weak.lock()) {
      if (watch->pointers.device == GpuProgressHost(base, device) &&
          !watch->cancelled) {
        watch->cancelled = true;
        ++state.statistics.cancelled;
        watch->wake.Notify();
      }
    }
}
void InvalidateAllGpuProgress() {
  auto &state = State();
  std::lock_guard lock(state.mutex);
  for (auto &weak : state.watches)
    if (auto watch = weak.lock()) {
      if (!watch->cancelled) {
        watch->cancelled = true;
        ++state.statistics.cancelled;
        watch->wake.Notify();
      }
    }
}
void GpuProgressRecovery() {
  std::lock_guard lock(State().mutex);
  ++State().statistics.recoveries;
}
ProgressStatistics GetGpuProgressStatistics() {
  std::lock_guard lock(State().mutex);
  return State().statistics;
}
} // namespace rr6
