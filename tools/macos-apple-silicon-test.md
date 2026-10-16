# Apple Silicon package test — 15 October 2026

This records a short hardware test of the native macOS support already merged
upstream. It is not a new port or a claim of full game compatibility.

## Download and source

The [community test release](https://github.com/channelramble/RidgeRacer6-Recompilation/releases/tag/macos-arm64-test-20261015)
contains the exact app archive used for this test and its SHA-256 checksum.
It is hosted on a contributor fork, not an official upstream release.

| Component | Revision |
| --- | --- |
| RR6 source and packager | `50d1c9d3268eff1ada34a413b026697a02331e0a` |
| Shared ReXGlue SDK | `e4a7f75499071fa773d16a099b3ec2b4f941f786` |

The archive was made with `macos/make-package.sh` using an existing native
`mac-arm64-release` build and the SDK above. No source content changes were
applied to those revisions. The checkout had only a generated file-mode change
on `rr6_recomp_manifest.toml`. The additional SDK changes proposed in SDK
PRs #1 and #5 are not included in this build. The archive's `BUILD.txt` also
records the MoltenVK revision.

The release tag points to the runtime's source revision. The checksum-output
change accompanying this report does not change the tested app: its checksum
was generated separately over the original archive.

The package includes the native launcher, executable, runtime libraries,
Vulkan/MoltenVK dependencies, default settings and licenses. It contains no
ISO, extracted disc files, generated C++ sources, DLC, saves, shader caches or
disc-derived icon. Bring your own Ridge Racer 6 USA disc image. The declared
minimum deployment target is macOS 14.0; that is not a claim that this test ran
on macOS 14. The app is ad-hoc signed and is not notarized.

## Hardware test

- Apple M4 Pro MacBook Pro running macOS 27, arm64.
- Native 1280×720 guest rendering, original frame rate, guest VSync enabled.
- The app was installed in `/Applications` and launched through the Ridge
  Racer Marathon native RR6 adapter using the owner's extracted USA game.
- All 45 extracted files were verified against the source copy with SHA-256.
- The supported USA executable hash was
  `39d3c0004ec62aeb0fe3e7e1889cf25d98fbd27987b6bc6b5ff30a56ffba6c00`.
- Menu navigation, entering a race and sustained keyboard acceleration were
  exercised. Captures at 120 and 160 seconds showed racing; the latter showed
  173 km/h. The integration run lasted 210 seconds with no launch failures.
- The game's reported 30-second frame-rate averages were 59.8, 59.3, 56.6 and
  60.0 fps. Initial shader-compilation stalls still occurred, with a reported
  maximum frame time of 551.8 ms. These are game statistics, not GPU timings
  or a controlled performance comparison.

## Limits

This was a short race and keyboard smoke test, not a completion playthrough.
Physical controllers, Intel Macs, older macOS versions, all tracks and cars,
long sessions, DLC and online features were not verified by this run. The test
used already extracted game data, so it does not validate the native launcher's
first-run ISO import or the downloaded app's Gatekeeper flow. Recording and
input automation were test-only instrumentation and are absent from the ZIP.
Retain the existing experimental status and shader-compilation caveats.
