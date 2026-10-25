#include "native_gpu_progress.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <future>
#include <iostream>
#include <stdexcept>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>
using namespace std::chrono_literals;
namespace {
constexpr uint32_t kDevice = 0x100, kWord = 0x5000;
struct alignas(32) Fixture {
  std::array<uint8_t, 0x6000> memory{};
  uint8_t *word = memory.data() + kWord, *physical = word;
  Fixture();
};
Fixture *current = nullptr;
std::atomic<bool> enabled = true;
void Check(bool ok) {
  if (!ok)
    throw std::runtime_error("GPU-progress bridge assertion failed");
}
uint32_t Read(uint8_t *p) { return rr6::GpuProgressRead32(p, 0); }
void Publish(uint8_t *p, uint32_t value, rr6::ProgressWriter kind) {
  rr6::GpuProgressWrite write(p, 4, kind);
  value = __builtin_bswap32(value);
  std::memcpy(p, &value, 4);
}
Fixture::Fixture() {
  current = this;
  rr6::GpuProgressStore32(memory.data(), kDevice + 10384, kWord);
  rr6::GpuProgressStore32(memory.data(), kDevice + 10396, 6);
}
} // namespace
namespace rr6 {
bool GpuProgressEnabled() { return enabled; }
std::optional<ProgressPointers> ResolveGpuProgress(uint8_t *base,
                                                   uint32_t device) {
  if (!current || base != current->memory.data() || device != kDevice)
    return std::nullopt;
  return ProgressPointers{base + device, current->word, current->physical,
                          kWord};
}
void ReportGpuProgress() {}
} // namespace rr6
namespace {
void ReadyTimerAndWriters() {
  Fixture f;
  auto before = rr6::GetGpuProgressStatistics();
  {
    rr6::GpuProgressScope scope(f.memory.data(), kDevice);
    scope.Park(
        4, 0); // Pending: timer recheck returns without fabricating progress.
    Check(Read(f.word) == 0 && rr6::GpuProgressPending(6, 4, Read(f.word)));
    for (size_t k = 0; k < size_t(rr6::ProgressWriter::Count); ++k) {
      Publish(f.physical, 2, rr6::ProgressWriter(k));
      scope.Park(4, 0);
      Check(Read(f.word) == 2);
    }
    Publish(f.physical, 4, rr6::ProgressWriter::Event);
    scope.Park(4, 2);
    Check(!rr6::GpuProgressPending(6, 4, Read(f.word)));
  }
  auto after = rr6::GetGpuProgressStatistics();
  Check(after.timer_rechecks > before.timer_rechecks &&
        after.parks > before.parks);
  for (size_t k = 0; k < size_t(rr6::ProgressWriter::Count); ++k)
    Check(after.writes[k] > before.writes[k]);
}
void AliasAndConcurrentProgress() {
  Fixture f;
  char path[] = "/tmp/rr6-progress-alias-XXXXXX";
  int fd = mkstemp(path);
  Check(fd >= 0);
  unlink(path);
  Check(ftruncate(fd, 4096) == 0);
  auto *a = static_cast<uint8_t *>(
      mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  auto *b = static_cast<uint8_t *>(
      mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  close(fd);
  Check(a != MAP_FAILED && b != MAP_FAILED && a != b);
  f.word = a;
  f.physical = b;
  {
    rr6::GpuProgressScope scope(f.memory.data(), kDevice);
    auto waiting = std::async(std::launch::async, [&] {
      const auto until = std::chrono::steady_clock::now() + 2s;
      while (rr6::GpuProgressPending(6, 4, Read(a)) &&
             std::chrono::steady_clock::now() < until)
        scope.Park(4, Read(a));
      return Read(a) == 4;
    });
    std::this_thread::sleep_for(5ms);
    Publish(b, 2, rr6::ProgressWriter::Event);
    std::this_thread::sleep_for(2ms);
    Publish(b, 4, rr6::ProgressWriter::Event);
    Check(waiting.get());
  }
  // Cancellation is checked before cached memory is dereferenced, even later.
  {
    rr6::GpuProgressScope scope(f.memory.data(), kDevice);
    rr6::InvalidateGpuProgressDevice(f.memory.data(), kDevice);
    Check(mprotect(a, 4096, PROT_NONE) == 0 &&
          mprotect(b, 4096, PROT_NONE) == 0);
    scope.Park(4, 0);
  }
  munmap(a, 4096);
  munmap(b, 4096);
}
void ChangedStorageAndAbort() {
  Fixture f;
  auto before = rr6::GetGpuProgressStatistics();
  {
    rr6::GpuProgressScope scope(f.memory.data(), kDevice);
    rr6::GpuProgressStore32(f.memory.data(), kDevice + 10384, kWord + 4);
    scope.Park(4, 0);
    Check(Read(f.word) == 0);
  }
  Check(rr6::GetGpuProgressStatistics().changed_storage ==
        before.changed_storage + 1);
  rr6::GpuProgressStore32(f.memory.data(), kDevice + 10384, kWord);
  {
    rr6::GpuProgressScope scope(f.memory.data(), kDevice);
    rr6::GpuProgressStore8(f.memory.data(), kDevice + 10433, 4);
    scope.Park(4, 0);
    Check(Read(f.word) == 0); // An abort wake is not a completed fence.
  }
}
void SelectionWrapAndBound() {
  Check(rr6::GpuProgressPending(6, 4, 0));
  Check(!rr6::GpuProgressPending(6, 4, 4));
  Check(rr6::GpuProgressPending(2, 0xFFFFFFFE, 0xFFFFFFFC));
  Check(!rr6::GpuProgressPending(2, 0xFFFFFFFE, 0xFFFFFFFE));
  Fixture f;
  auto before = rr6::GetGpuProgressStatistics().scopes;
  std::vector<std::unique_ptr<rr6::GpuProgressScope>> active;
  for (int i = 0; i < 65; ++i)
    active.push_back(
        std::make_unique<rr6::GpuProgressScope>(f.memory.data(), kDevice));
  Check(rr6::GetGpuProgressStatistics().scopes == before + 64);
  rr6::InvalidateAllGpuProgress();
  for (auto &s : active)
    s->Park(4, 0);
  active.clear();
  enabled = false;
  {
    rr6::GpuProgressScope original(f.memory.data(), kDevice);
    original.Park(4, 0);
  }
  Check(rr6::GetGpuProgressStatistics().scopes == before + 64);
  enabled = true;
}
} // namespace
int main() {
  try {
    ReadyTimerAndWriters();
    AliasAndConcurrentProgress();
    ChangedStorageAndAbort();
    SelectionWrapAndBound();
    std::cout
        << "GPU progress: aliases, publication, counters, timer checks, "
           "cancellation, storage changes, wrap and bounded fallback passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
