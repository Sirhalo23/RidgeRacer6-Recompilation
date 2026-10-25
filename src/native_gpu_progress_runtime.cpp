#include "native_gpu_progress.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
REXCVAR_DEFINE_BOOL(rr6_native_gpu_progress_wait, true, "RR6",
                    "Use native notifications at the verified GPU-progress "
                    "poll (opt-in build)");
namespace rr6 {
bool GpuProgressEnabled() {
  // Fixed for this process; configure by command line, not during a live wait.
  static const bool enabled = REXCVAR_GET(rr6_native_gpu_progress_wait);
  return enabled;
}
std::optional<ProgressPointers> ResolveGpuProgress(uint8_t *base,
                                                   uint32_t device) {
  auto *kernel = rex::system::kernel_state();
  if (!kernel || !device || uint64_t(device) + 10492 >= 0x100000000ull)
    return std::nullopt;
  const auto word = GpuProgressRead32(base, device + 10384);
  if (!word || (word & 3))
    return std::nullopt;
  auto *memory = kernel->memory();
  const auto physical = memory->GetPhysicalAddress(word);
  if (physical == UINT32_MAX || physical > 0x1FFFFFFC)
    return std::nullopt;
  return ProgressPointers{memory->TranslateVirtual<uint8_t *>(device),
                          memory->TranslateVirtual<uint8_t *>(word),
                          memory->TranslatePhysical<uint8_t *>(physical), word};
}
void ReportGpuProgress() {
  const auto s = GetGpuProgressStatistics();
  REXLOG_INFO("[native-progress] scopes={} waits={} probes={} parks={} "
              "timer_rechecks={} "
              "event_writes={} cpu_writes={} cancelled={} changed_storage={} "
              "recoveries={}",
              s.scopes, s.waits, s.probes, s.parks, s.timer_rechecks,
              s.writes[size_t(ProgressWriter::Event)],
              s.writes[size_t(ProgressWriter::Cpu)], s.cancelled,
              s.changed_storage, s.recoveries);
}
} // namespace rr6
