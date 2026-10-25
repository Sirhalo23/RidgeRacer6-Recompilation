# Native waits in the existing renderer

This branch ports the audio and GPU-progress wait improvements without the
native renderer/HUD experiments. All implementation changes live in RR6.
The existing SDK still owns the renderer, audio processing, handle state and
other runtime services.

## Defaults and fallback

Native audio waits default ON for macOS ARM64 **source builds using the reviewed
SDK files**. CMake checks both source digests before choosing that default;
unreviewed SDK revisions and other host configurations default OFF. Explicitly
requesting an unsupported adapter/revision fails configuration with an error
rather than applying an unreviewed modification. An existing cached OFF setting
stays OFF until explicitly changed.

- Build audio in: `-DRR6_NATIVE_AUDIO_WAIT=ON`.
- Disable audio integration entirely: `-DRR6_NATIVE_AUDIO_WAIT=OFF`.
- With integration built in, `--rr6_native_audio_wait=false` selects the original
  audio wait route at runtime; the default is true.
- GPU progress waits stay experimental, default OFF. Build them with
  `-DRR6_NATIVE_GPU_PROGRESS_WAIT=ON`; then
  `--rr6_native_gpu_progress_wait=true/false` selects the process-start route.

Both adapters currently require POSIX SDK source targets. Windows keeps the
existing route. Linux is opt-in and has not been live-validated. No new Vulkan
or Metal backend is introduced.

`native/audio_wait.cmake` and `native/gpu_progress.cmake` prepare version-checked
copies inside the build directory. The SDK checkout and generated guest source
checkout are not modified by the adapters. The GPU copies are refreshed after
codegen and before compilation. No SDK commit, fork update or SDK installation
is needed for these adapters beyond the source checkout already used by RR6.

## What changes

The SDK-free C++20 `recomp::native::WaitSet` owns a condition variable and a
notification generation. Producers publish state then notify. Waiters probe
caller-owned state under its own locks and only park if the generation remains
unchanged, preventing lost wakeups. The library does not mirror semaphore
counts, console handles or guest GPU completion values.

Audio registration, ready-handle selection, semaphore consumption, callback
handling and cancellation use the SDK's existing state locks. Other threads and
unsupported waits retain the original route. Mixing, SDL output and XMA decoding
are unchanged.

GPU progress parking is inserted at `sub_8225B038`'s original polling point.
Reviewed guest and common SDK progress writers publish notifications, including
both virtual/physical host aliases. The original unsigned-distance readiness
comparison, preflush and watchdog/recovery helper remain. Cancellation detaches
watchers before storage reset. A 1 ms bounded recheck keeps the original helper
running even without notifications. A wake never fabricates completion. The
progress word represents command consumption, not a hardware GPU fence.

## Measurements and remaining validation

Measurements were taken on macOS ARM64/Vulkan before this focused port, using
the same wait implementation in the experimental branch:

- Audio startup: roughly 95% of one core on the original route versus 0.47%
  with native waiting in controlled same-binary comparisons.
- GPU progress startup: guest worker 97.24–98.01% versus 2.35–2.38% of one core,
  with similar guest-swap pacing in original/native/native/original runs.
  Native audio was enabled throughout, isolating the GPU wait change.
- Driven GPU-wait race: busiest non-main guest worker 9.21–9.98% of one core,
  no watchdog recoveries in three 20-second windows; user reported normal
  gameplay/audio. The earlier audio-only race still had a guest worker at
  96.9–97.9%. Different driving sequences prevent a controlled race percentage.

Race timing outliers remain, including pipeline creation and screenshot
confounders. These measurements establish CPU efficiency, not an FPS increase
or a proven pacing improvement. GPU waiting remains opt-in pending matched,
warmed-cache pacing comparisons and broader recovery/host coverage.

## Tests

SDK-free tests (no game files or GPU required):

```sh
cmake -S . -B out/build/native-waits -DRR6_NATIVE_ONLY=ON
cmake --build out/build/native-waits
ctest --test-dir out/build/native-waits --output-on-failure
```

For a full game source build, enable `-DRR6_NATIVE_TESTS=ON` to add shared wait
and GPU bridge tests. Audio integration builds actual SDK-handle tests by
default. GPU integration adds pinned original-versus-native guest-body tests.
Build the test targets (or the default all target) before running CTest.

Coverage includes producer/park races, ready-handle ordering and counts,
callbacks, timeout and cancellation behavior, distinct shared-memory aliases,
counter wrap and storage invalidation. Pinned guest fixtures compare memory,
service calls, nonvolatile registers, stack, link register and recovery/trap
behavior. Source preparers reject unsupported revisions before writing output.
