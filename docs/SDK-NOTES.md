# Notes for the ReXGlue SDK (v0.10.0), found while bringing up Ridge Racer 6

Things in the SDK (not in the game) that this project had to work around.
Each one is small and self-contained; they are written so they can be filed
upstream as separate reports. File and line references are to the v0.10.0
source tree.

## 1. FP control word is not seeded on SDK-created host threads

`ctx->fpscr.InitHost()` is only called for guest-created threads. The audio
worker (which runs the guest's XAudio render callback) and the graphics
interrupt thread enter guest code with MXCSR state the recompiled code does
not expect; as soon as the guest toggles flush-to-zero it unmasks FP
exceptions and the process dies with `0xC000008F` (float inexact result).

- Seen in: RR6 audio mixer `sub_822A8AE0`, called from the render callback
  `sub_822AA050`; also possible from the interrupt callback `sub_8225A590`.
- Workaround here: `src/host_thread_fixes.cpp` calls `InitHost()` on entry to
  both callbacks; `src/fp_guard.cpp` is a safety net.
- Suggested fix: initialise the guest FP state wherever the SDK builds a
  `PPCContext` for a host thread that will run guest code.

## 2. A missing content package is reported with the wrong error

`XamContentCreate` with OPEN_EXISTING on content that does not exist completes
the overlapped operation with `ERROR_PATH_NOT_FOUND` (3)
(`src/kernel/xam/xam_content.cpp`). The console reports this case differently;
RR6's save library accepts only `ERROR_FUNCTION_FAILED` (0x65B) as "no save
yet" and treats anything else as a broken storage device.

- Workaround here: `src/save_fixes.cpp` rewrites 2/3 to 0x65B for content opens.
- Worth checking what real hardware returns before changing the SDK; other
  titles may depend on a different value.

## 3. The save thumbnail is stored inside the save's own folder

`ContentManager::SetContentThumbnail` writes `__thumbnail.png` into the content
package directory (`src/system/xam/content_manager.cpp`,
`kThumbnailFileName`). That directory is what the game sees as the root of its
mounted package, so the file shows up in the game's own directory listing.

RR6 lists `save:\` and takes the first entry as its save file (its data file
has a different encoded name on every save). When `__thumbnail.png` is listed
first, the game reads the PNG, fails validation and reports "Game Data is
corrupted". On NTFS the listing is alphabetical, so it depends on the data
file's name; on Linux (hash order) it happened on the first save.
Reproduced deterministically: same save folder, thumbnail present and listed
first -> corrupted; thumbnail deleted -> loads.

- Workaround here: `src/save_fixes.cpp` does not forward
  `XamContentSetThumbnail` (cvar `rr6_save_thumbnail`), and the launcher
  deletes thumbnails left by earlier builds.
- Suggested fix: keep the thumbnail next to the package header (the
  `Headers\...` tree) or hide it from guest directory queries.

## 4. Direct3D 12: slope-scaled depth bias is part of the pipeline key

During a race RR6 sets a different `D3DRS_SLOPESCALEDEPTHBIAS` for about 13
draws per frame, all with one shader pair (VS `9AFE6F56B57E8F8D` / PS
`699D9C919C528F8D`; quad lists, stencil test, no depth write). On the non-ROV
Direct3D 12 path that float goes into `PipelineDescription`
(`rex/graphics/d3d12/pipeline_cache.h`, `depth_bias_slope_scaled`) unrounded,
so every draw needs a new pipeline, and every pipeline is written to the
storage and rebuilt at the next start.

Measured on Windows (RTX 3060 Ti): no pipeline creation in the menus, about
770 per second for the length of a race. After a few short sessions the
storage held 273,327 pipelines ("Created 248770 graphics pipelines ... from the
storage in 876 milliseconds" at the start of the last of them); 273,202 belong
to this shader pair and differ in that one field only, with values from 0.25
to 74,475. The Vulkan backend, which sets depth bias dynamically, stores a
handful. (`tools/xpso_stats.py` prints the per-field counts of a storage file.)

- Workaround here: `src/depth_bias_fix.cpp` rounds the value in the game's
  render-state setter to eight values per power of two (rounding up), which
  leaves a few dozen pipelines; the launcher deletes a `*.d3d12.xpso` file
  above 4 MB left by earlier builds.
- Suggested fix: quantise the slope-scaled bias before it enters the
  description, use dynamic depth bias where the device supports it
  (`ID3D12GraphicsCommandList9::RSSetDepthBias`), or cap what is persisted.

## 5. Function-pointer table tail call recompiled as a `switch`

Two RR6 functions (`sub_8217C938`, `sub_8217D288`) end in
`lwzx; mtctr; bctr` through a table of *function pointers* (tables at
0x8236D1B0 and 0x8236D1E8). The analyser treated the tables as jump tables
of the function itself and emitted a `switch` whose default case traps
(`0xC000001D`).

- Workaround here: `src/dispatch_fixes.cpp` replaces both with
  `REX_CALL_INDIRECT_FUNC`.
- The manifest has an `indirect_calls` hint, but v0.10.0 does not read it.
- Suggested fix: when the table's targets are function entry points outside
  the current function, emit an indirect call.

## 6. Functions reached only through pointers are not discovered

36 functions that are only referenced from data (vtables, callback tables,
the static-constructor table) and 7 leaf functions reached only by tail-call
thunks had to be listed by hand in the manifest. `tools/find_orphan_targets.py`
finds them by scanning data and `lis/addi` pairs for addresses that land on a
`.pdata` function start.

## 7. Smaller observations

- `present_effect` only accepts `bilinear` in the prebuilt Windows runtime
  (FidelityFX is not compiled in), although the help text lists cas/fsr.
- Setting `window_width`/`window_height` also changes the guest video mode
  (`GetConfiguredVideoModeWidth` in `xboxkrnl_video.cpp`), so they cannot be
  used just to size the window.
- With no audio output device the SDL audio backend has nothing to open and
  RR6 dereferences a null pointer shortly after audio initialisation. The
  launcher warns before starting; a null sink in the SDK would be cleaner.
- On Linux the runtime re-reads `/sys/devices/system/cpu/online` constantly
  (hundreds of thousands of opens in a few minutes) and logs "Too few
  processor cores" on every affinity call when the host has fewer than six.
- The SDK audio worker thread busy-waits: it used 40-90% of one core in every
  Linux test run.
- A key binding made with `rex::ui::RegisterBind` appears to fire again for
  every repeat of a held key: a binding that toggled a window flipped it back
  while the key was held. `KeyEvent::prev_state()` marks the repeats correctly
  (seen in the log), so the bind dispatcher could skip them.
- With the SDK's F3 window open, the game received no keyboard-as-controller
  input; with only an app's own `ImGuiDialog` open it did. Seen on Linux, not
  tried with a real pad or with the F4 window. Not documented either way.
