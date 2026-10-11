# Reproducible macOS performance measurements

Use the native arm64 Release build on Apple silicon. Record the game and SDK
commit IDs, macOS version, chip, display refresh rate, power mode and the exact
settings with every capture. Keep the car, track, camera, opponents, replay and
capture duration the same. Compare at least three runs per build, alternating
before/after order where possible. Loading, menus, the first race and subsequent
races are separate workloads; avoid combining them into one FPS number.

## CPU frame timing CSV

The SDK used by this checkout supports `--perf_log_csv=/absolute/path.csv` in
Release without enabling the per-draw atomic performance counters or Tracy.
Rebuild against the updated SDK before using it. The former SDK flag existed
but did not open a CSV, and its counter macros were disabled in Release.

```sh
mkdir -p logs/perf out/perf-cache/cold-01
./run-macos.sh \
  --cache_root="$PWD/out/perf-cache/cold-01" \
  --perf_log_csv="$PWD/logs/perf/cold-01.csv"
```

Quit the game normally after the chosen sequence. The file opens at graphics
initialization, overwrites the specified path and flushes every 60 guest swaps;
normal shutdown closes it. Use a different output filename for each capture.
An empty path disables capture, and changing the path requires a restart.

The CSV contains `frame_index`, `elapsed_us`, `frame_time_us` and
`issue_swap_cpu_us`. Frame time is the CPU interval between guest XE_SWAP
commands. IssueSwap time includes the CPU work and any waits inside that call.
Neither value measures GPU execution or the number of frames actually displayed.
In particular, asynchronous pipeline compilation can skip presentation while
guest swaps continue. The first row has no previous interval and is excluded
from statistics. Timing and buffered file writes add some overhead, so compare
captures made with the same instrumentation and repeat a final check without it.

```sh
python3 tools/perf_summary.py logs/perf/cold-01.csv
python3 tools/perf_summary.py logs/perf/warm-before.csv logs/perf/warm-after.csv \
  --skip-seconds 60 --duration 120
python3 tools/perf_summary.py logs/perf/warm-after.csv --json
```

Choose the skipped period and window from the actual sequence; 60 seconds is
only an example. The summary reports interval-weighted FPS, nearest-rank p50,
p95 and p99 frame times, worst frame, frames over the target budget, and stalls
over twice that budget. `--target-fps` defaults to 60; `--stall-ms` overrides the
stall threshold. Its window includes rows whose interval endpoint is within the
selected elapsed period. Use percentiles and stall counts alongside the average.
The parser also accepts the older counter CSV schema. Zero Vulkan cache/stall
counters are not evidence of hits or absence of stalls: that backend's existing
counter call sites are missing. The new lightweight CSV deliberately omits them.

## Cold and warm shader caches

For a cold application cache, create a new `--cache_root` directory for each
run. Keep saves and game files in their normal folders. Run the chosen sequence
once with that cache, then repeat it with the same directory and a new CSV
filename for a warm capture:

```sh
./run-macos.sh \
  --cache_root="$PWD/out/perf-cache/cold-01" \
  --perf_log_csv="$PWD/logs/perf/warm-01.csv"
```

Create independent cold directories for before and after builds, and warm each
with the same sequence. To compare against an already populated cache, make a
copy for each build while the game is closed. Preserve originals; the cache
may be updated during each run. A fresh application directory does not clear
macOS/Metal driver caches, so label it "cold application cache", not "cold GPU".
Record shader/pipeline loading and compilation messages from the game log
alongside the CSV. Keep async compilation, render scale, Retina output, texture
filtering and presentation mode identical across comparisons.

## Native CPU and GPU traces

Use the Instruments templates installed with Xcode to determine which thread
or native GPU work causes a stall. List this machine's templates first:

```sh
xcrun xctrace list templates
```

Time Profiler provides CPU stack samples. Metal System Trace, when installed,
can inspect MoltenVK's native Metal command buffers, GPU activity and waits.
Capture the same race segment and inspect whether long CPU swap intervals
coincide with GPU work, CPU submission or shader compilation. Do not infer GPU
utilization from CPU sample counts or the CSV. Instruments may require macOS
profiling permissions, and tracing can add overhead; collect it separately
from ordinary CSV comparisons.

The summary parser's deterministic checks run with:

```sh
python3 -m unittest discover -s tools -p 'test_perf_summary.py' -v
```
