# recomp_native wait component

SDK-independent C++20 notification-driven waiting for caller-owned host state.
The focused main-branch port contains only `WaitSet`, its tests and license;
renderer components remain on the native-library development branch.

`WaitUntil(probe, deadline)` invokes the caller's readiness probe outside the
native mutex. The probe checks/consumes state under the caller's own locks and
returns an optional result index. Producers publish state before `Notify()`.
A generation check prevents lost wakeups between probing and parking.
`Clock::time_point::max()` means indefinite waiting. A ready result wins over
an expired deadline; otherwise an expired deadline returns `nullopt`.

The caller owns selection/cancellation policy, counters and payload lifetimes.
Join producers and waiters before destroying a set. Notification is not
async-signal-safe. `GetStatistics()` exposes cumulative waits, probes, parks,
notifications and timeouts. Public API stability is not yet promised.

Build independently:

```sh
cmake -S libs/recomp_native -B out/build/recomp-native
cmake --build out/build/recomp-native
ctest --test-dir out/build/recomp-native --output-on-failure
```

Consumers can `add_subdirectory(path/to/recomp_native)` and link
`recomp::native`. No SDK, game, GPU or window dependency is required.
See [RR6 integration](../../docs/NATIVE-WAITS.md) for defaults and controls.
