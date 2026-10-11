# macOS performance findings — 11 October 2026

The native arm64 Release build now reuses presenter pipelines, submits Metal
work through MoltenVK's serial dispatch queue, persists Vulkan driver shader
data and blocks idle timer waits. Existing visual settings are retained.

## Changes

- **Presenter pipeline reuse:** the cached swapchain format was never recorded
  after pipeline creation. Every subsequent paint treated it as incompatible,
  waited for the previous GPU submission, destroyed it and compiled another.
  The successful creation path now records its format. Real format changes
  retain the existing wait/invalidation path; effects retain separate entries.
- **Asynchronous MoltenVK submissions:** default
  `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=0` moves drawable waits off the UI thread
  and out of the SDK queue mutex. Submission order, fences and queue locking
  remain intact. An explicit environment value overrides this default.
- **Persistent Vulkan pipeline cache:** guest pipeline creation shares a
  `VkPipelineCache`. Device/driver-specific cache files use size/hash/header/UUID
  validation and atomic replacement. Invalid files safely rebuild. MoltenVK
  stores translated MSL here; native Metal compilation can still occur.
- **Normal-exit cache flush:** the app uses a hard exit that bypasses destructors.
  It now explicitly snapshots the driver cache and flushes buffered guest shader
  descriptions before exit. The writer flush acknowledgement follows actual
  writes, with a two-second wait limit for the guest-storage writer.
- **Timer queue:** replace frequent idle polling with deadline-aware condition
  variable waits; correct the vendor wait_until argument order.
- **Optional Release measurements:** `perf_log_csv` now records CPU guest-swap
  intervals and IssueSwap time. It is disabled unless a path is supplied.
- **Idle achievement popup:** remove the popup from the overlay drawer once
  startup work is complete and nothing is showing. Notifications reattach on
  the UI thread; silent unlocks also reattach briefly to refresh launcher data.
- **F3 frame statistics:** count guest frame production in the game's swap
  routine and supply the SDK overlay's frame statistics provider.

## Measurements

Machine: Apple M4 Max, 14 CPU cores, 36 GiB RAM, macOS 27.0.1.
Native arm64 Release, MoltenVK 1.4.3, scale 1 / 720p guest rendering, logical
presentation buffer, guest VSync enabled, async guest shader compilation enabled.
Tests used isolated user-data copies. Neither the user's saves nor settings were
changed. macOS/Metal caches were not purged; power mode and display refresh were
not independently recorded.

Before the presenter reuse fix, toggling only MoltenVK submission mode in the
same instrumented build gave the following opening Pac-Man results. Each row
uses seconds 2–18 from a separate launch, before menu input:

| Submission mode | Guest swaps/s | Mean IssueSwap CPU | p95 IssueSwap CPU | Worst guest interval |
| --- | ---: | ---: | ---: | ---: |
| Synchronous | 59.88 | 4.774 ms | 5.983 ms | 33.791 ms |
| Asynchronous | 60.00 | 0.029 ms | 0.047 ms | 18.837 ms |

Two subsequent asynchronous opening captures reproduced mean IssueSwap CPU
of 0.029 and 0.036 ms, approximately 60 guest swaps/s, and no interval above
33.333 ms. This demonstrates much less CPU work/waiting inside IssueSwap;
it is not a claim of a 99% overall FPS increase.

A 20-second Lakeshore Drive Class 1 / Sky Kid Fiera / AT segment with the
asynchronous mode ran at 59.96 guest swaps/s, p95 interval 17.835 ms,
p99 18.405 ms and worst 18.857 ms, with zero intervals above 33.333 ms.
Driving, menus and race rendering were visually checked. This segment does
not represent all tracks or a full race.

With all fixes, a separate 20-second driving segment measured 59.96 guest
swaps/s, p95 interval 17.851 ms, p99 18.270 ms and worst 20.160 ms, with zero
intervals above 33.333 ms. Mean IssueSwap CPU was 0.084 ms. This ran after
the short profile ended, with no compiler or profiler running.

After integrating the idle-popup and F3 changes, a final opening capture
(seconds 2–18) measured 60.00 guest swaps/s, p95 interval 17.768 ms,
p99 18.387 ms and worst 18.726 ms, with zero intervals above 33.333 ms.
Mean IssueSwap CPU was 0.051 ms. Idle detachment, F3 statistics, achievement
list opening/closing and normal quit were checked in the native build.
The earlier driving capture predates these two game-level changes.

Five-second CPU samples before the presenter fix showed repeated presenter
pipeline creation and the associated completion wait. In the async capture,
1,144 of 1,352 main-thread samples were in that wait and 147 were in
CreateGuestOutputPaintPipeline. Drawable waits moved to MoltenVK's dispatch
queue and the SDK submission mutex ceased to dominate the GPU worker.
Sample counts identify blocking locations; they are not GPU utilization.
After the presenter fix, a steady opening-scene sample contained no
CreateGuestOutputPaintPipeline calls; the remaining completion wait was at
the normal in-flight submission limit. Its startup CSV overlaps sampling
attachment and is excluded from performance comparisons.

Normal exit saved a 3,155,410-byte Vulkan driver cache; the next launch logged
that exact payload loaded. Guest shader files also grew from buffered entries.
The idle timer benchmark reduced thread CPU from approximately 7.2–7.7 ms per
second to 0.009–0.010 ms per second, without worsening measured deadline latency.

CPU guest swaps can continue while asynchronous shader compilation skips
presentation. These CSV numbers do not measure GPU execution or actual displayed
FPS. First visits to new scenes may still hitch. Longer repeated race runs and
Metal System Trace are needed to quantify displayed frame delivery across tracks.

Guest-code inspection found GPU-progress polling in `sub_8225B038` /
`sub_82262C20` and readiness polling with an optional callback in
`sub_82144DA0`. Replacing these waits with host events or a cooperative wait
is a potential follow-up, after identifying every writer and preserving
callbacks/timeouts. The sampled race main thread mostly slept; no guest math
replacement was justified by these captures.

## Build and verification

The runtime changes are proposed in
[shared SDK PR #5](https://github.com/Sirhalo23/rexglue-sdk/pull/5), following
[SDK PR #1](https://github.com/Sirhalo23/rexglue-sdk/pull/1). The measurements
below used SDK commit `dda8276` and game changes based on `24d6171`. The review
branch also incorporates current upstream game `main` (`53e3d3d`), retaining
its music-loop, Windows GPU-selection and thread-diagnostic fixes.

Use the companion SDK PR while it is pending, then rebuild the game, runtime
and GPU plugin together because their shared graphics interface changes:

```sh
git -C ../rexglue-sdk fetch origin pull/5/head
git -C ../rexglue-sdk checkout FETCH_HEAD
git -C ../rexglue-sdk submodule update --init --recursive
./build-macos.sh
```

Validation: native Release build; timer regression tests (4 cases / 10
assertions), 9 cache integrity/fallback checks, 100 rounds of buffered writer
flush checks, and 6 timing-summary parser tests. Runtime cold-save/warm-load
checks exercised actual MoltenVK rather than a fake driver.

Local measurements, logs and CPU samples are under `out/perf/`. For capture
commands, see [performance.md](performance.md). To compare submission modes,
set `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=1`. To disable driver caching for an
A/B run, pass `--vulkan_driver_pipeline_cache=false`. Keep separate cache copies
and capture filenames for each build and mode.

## Additional branch review

Selectively adapted the idle-popup change from game commit `32fd28e` on
`origin/perf-popup-waits`, adding reattachment for silent unlocks to preserve
launcher exports. The F3 provider from game commit `57021e9` has since landed
in upstream main; this PR retains clarifications that it measures guest frame
production. Current main's Windows GPU-selection/timer diagnostics are also
retained, though they do not improve the macOS path.

A read-only remote check on 11 October confirmed that the maintainer SDK's
`rr6` branch still points to `e4a7f75`, already included in the local SDK base
`1cc9612`. Three relevant fixes remain on separate branches, each one commit
ahead of `rr6`:

| SDK branch | Commit | Scope and decision |
| --- | --- | --- |
| `funclet-handover` | `70c444d` | Shares every localized register with SEH funclets and keeps funclet registers in `ctx`. A prerequisite for register-local experiments; not applied here. |
| `silent-audio` | `21be00e` | Uses a paced silent driver when no audio device opens, preventing RR6's null-audio startup crash. No expected performance benefit with working audio; not applied here. |
| `display-vsync` | `7a3dfb7` | Adds display-paced guest timing on Direct3D 12 and corrects XMA loop-data decoding. Vulkan only gains FIFO selection, already available through the existing presentation-mode settings; not applied here. |

The game branch `origin/perf-popup-waits` at `a700024` documents the register
options separately. Its earlier crashes with every option enabled were
confounded by an unavailable audio server. With working audio,
`non_volatile_as_local` and the cr/xer/ctr/reserved options drew frames on the
maintainer's software-rendering rig, but those tests establish no M4 Max speedup.
`non_argument_as_local` still hangs at startup with the funclet fix: the C++
compiler hoists a guest memory load out of the polling loop in `sub_82144DA0`.
The funclet fix does not solve that loop-load problem. Register-local options,
`skip_lr`, and `skip_msr` remain disabled in this build.

Upstream SDK `development` also contains unintegrated code-generation changes:
`bd833a2` emits fences for `sync`, `lwsync`, and `eieio`; `7f7c92e` changes
store-conditional generation to C++ atomic compare/exchange; `ea222e9` fixes
the associated unsigned register operands; and `6319e23` fixes vector packing
when destination and source registers alias. The missing fences are especially
relevant to ARM64 correctness because the current generator treats these guest
barriers as no-ops under an x86 ordering assumption. These warrant a separate
regeneration and correctness-testing follow-up, not an unmeasured performance
claim or a blanket SDK branch merge.
