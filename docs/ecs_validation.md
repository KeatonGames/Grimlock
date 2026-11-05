# ECS validation and profiling (Phase 37)

## Audit and corrections

The existing registry and all four actor owners were reviewed for generation-safe references, packed relocation, deferred mutation, ownership, update order, selection, and teardown. The implementation retains the current single-threaded pipeline and coarse components; no gameplay or storage algorithm was replaced.

Two correctness problems were fixed:

- Component pools could be copied/moved through the public typed storage reference, allowing accidental ownership transfer outside the registry. Pools are now noncopyable/nonmovable; raw type-erased erase is registry-private.
- A failed allocation while reactivating a tower could leave its body active without attack state. Combat assumes both exist. Reactivation now rolls the body back before propagating the exception, retaining identity and progress. An injected-failure test reproduced this before the fix and passes afterward.

Health/damage helpers, stable spawn-order targeting, Scene-ID tower order, per-owner generation handles, navigation revision checks, and Engine-owned GPU resources remain authoritative. Scene-to-tower synchronization is a boundary snapshot, not a second gameplay owner. Current usage and component/system authoring are documented in [Gameplay ECS](ecs.md).

## Added validation

- Compile-time pool ownership restrictions.
- Two identical seeded 20,000-operation histories, checked against a map oracle: create, remove, destroy, clear, joins, sorting, stale handles, and deterministic results.
- Deferred destroy/remove queue allocation failure and subsequent recovery.
- Tower reactivation failure injection across storage growth: 48 failing allocation points exercised after optimization, with body/attack invariants and retained progress checked. This broad runtime injection runs in unchecked Release builds; MSVC checked STL can allocate proxy state within `noexcept` operations and abort before the test's exception boundary. The initial Debug injection attempt timed out on that CRT abort dialog; normal Debug actor checks remain enabled.
- Tower source edits after disable/re-enable, including model/type/transform changes.
- Repeated real level loading (`first_watch`, `hill_watch`, `first_watch`), wave start, unload, and render-instance cleanup.
- Existing actor stale-target, selection, death, projectile, combat, and ownership regressions remain enabled.
- Optional standalone AddressSanitizer target; it passed with the current core tests. This is not a sanitizer build of the entire Vulkan application or a native/GPU leak audit.

## Measurement method

Release x64, 960x540, fixed 1/60 simulation step, 30 warmup frames, 180 measured frames. Three alternating baseline/optimized runs used the same instrumented harness and machine, with Vulkan validation disabled. Cases include mixed enemy archetypes and one boss, three heroes, eight towers (four of each type), and a fixed 128-projectile volley. The 500-enemy case is opt-in. Hardware/driver and desktop scheduling affect timings; these are local measurements, not portable throughput guarantees.

The baseline binaries were saved before production changes. All paired cases had identical measured combat checksums, impact counts, defeats, and survivor counts. The final harness additionally measures an isolated projectile update probe after the main measured frames.

| Enemies | Mean frame ms before / after | p95 ms before / after | p99 ms before / after | C++ new calls before / after |
| --- | --- | --- | --- | --- |
| 100 | 4.236 / 4.140 | 5.668 / 4.800 | 6.478 / 5.018 | 12,418 / 3,778 |
| 200 | 4.142 / 4.120 | 5.131 / 5.094 | 6.551 / 5.383 | 12,467 / 3,827 |
| 500 | 5.562 / 5.560 | 5.088 / 6.987 | 12.431 / 13.269 | 16,066 / 7,882 |

Allocation counts cover all 180 measured updates, not one frame. At 200 enemies, requested bytes fell from 1,069,995 to 793,515. Excluding the first 31 sampled frames, calls fell from 7,523 to 371. Remaining allocations include projectile/render-instance creation and occasional navigation paths.

| 200-enemy stage | Before ms | After ms |
| --- | --- | --- |
| Enemy AI/targeting and synchronization | 0.037 | 0.028 |
| Movement/navigation | 0.747 | 0.748 |
| Hero update | 0.005 | 0.006 |
| Tower combat/projectiles | 0.017 | 0.017 |
| Render preparation | 0.018 | 0.019 |
| Render | 3.314 | 3.300 |
| GPU timestamps | 1.084 | 1.355 |

Process private memory at 200 enemies averaged 1811.0 / 1811.2 MiB; working set 1393.9 / 1393.4 MiB. This does not demonstrate a reduction in retained process/GPU memory.

## Changes justified by measurements

1. Unchanged tower synchronization reconstructed path-bearing snapshots. A 1,000-call probe performed 16,000 allocations requesting 512,000 bytes. Reusing snapshot elements and comparing model paths before assignment reduced this to **zero allocations**, around 3.2 ms to 2.0 ms locally. Source changes still refresh the same fields and definitions.
2. One million generic component lookups took 18.65 ms; cached typed-pool lookup took 0.76 ms and joined iteration 2.56 ms, all allocation-free after reserve. Enemy/hero/tower owners now retain their team pools and expose `team(id)` for hot combat checks. The generic registry API and component layout are unchanged. The final access probe measured 18.69 / 0.73 / 2.46 ms respectively.

The core 100,000-operation create/join/update/deferred-destroy probe measured 7.54 ms before and 8.67 ms afterward in individual runs. This was not the optimized path, and single-run differences do not establish a throughput gain. No overall FPS improvement is claimed. At 500 enemies, AI/navigation retry spikes remain around 140 ms; p95/p99 worsened in these short runs despite unchanged outcomes and allocations falling. These remain a performance limitation; this phase does not change navigation algorithms to mask them.

The final isolated 128-projectile probe took 0.031 / 0.034 / 0.026 ms per update
at 100 / 200 / 500 enemies, with zero allocations over its 32 updates after spawn.
Spawn measured 109.5 / 133.6 / 271.8 ms, including lazy resource/navigation work;
the measured cleanup portion took 0.54 / 0.80 / 1.27 ms (five allocations, 200 bytes).
These additional probes run after the main measured combat frames.

## Reproducing profiles

```powershell
cmake --build --preset release --parallel 4
$env:VK_INSTANCE_LAYERS = ''
& out/build/release/tests/Release/EcsChecks.exe
& out/build/release/Release/CombatStressChecks.exe 180 --large
& out/build/release/Release/CombatStressChecks.exe 180 --animated
```

The existing Editor navigation performance display remains opt-in. Test-only allocation replacements are linked only into ECS/stress checks, not Game or Editor. Counters measure successful C++ `new` calls reaching those replacements and cumulative requested bytes on the calling thread, including nested subsystem scopes. They exclude direct `malloc`, other threads, private dependency allocators, driver/native allocations, and retained/live heap size. Scopes unwind safely and support deterministic failure injection.

CSV `STRESS` retains its original first 24 columns and appends total frame calls/bytes, steady calls (sample index >30), and AI/movement/hero/combat calls. `TOWER_SYNC` reports 1,000 calls; `PROJECTILES` reports average time over 32 updates and total allocations; `LIFECYCLE` reports spawn/cleanup times and allocations. Spawn includes lazy model/navigation setup. Cleanup measures hero clear/navigation shutdown after projectiles have already been cleared; it is not a full-process unload measurement.

## Verification

- Complete Release and Debug builds succeeded, including `Grimlock.exe` and `GrimlockEditor.exe`.
- Release: **41/41 tests passed**, 146.99 s, with Vulkan validation enabled. Final Release ECS rerun: **2/2 passed** after the test-harness adjustment.
- Debug: the full run passed **40/41**; the broad allocation-injection CRT abort described above caused the sole timeout. After restricting that case to unchecked builds, **Ecs.Runtime passed on rerun**, 5.46 s. All 41 Debug checks therefore passed across the full run and focused recovery; no second full-suite run is claimed.
- Standalone AddressSanitizer: **1/1 passed**, 0.31 s.
- Final mixed 100/200/500 stress run passed; unchanged main-frame checksums matched the saved baseline. Modified C++ passed root clang-format verification and `git diff --check`.
- Animated 50/100/200-enemy stress run passed; the 200-enemy case averaged 4.176 ms/frame, including 2.538 ms animation update. This separate animated run is not a before/after comparison.

Local logs and paired CSV output are in `out/archive/phase37/`. Normal builds leave the optional sanitizer target disabled. Existing external GLM CMake deprecation, local shadowing, and model-import warnings are unrelated to this phase. Automated Game/UI/Editor runtime checks were exercised; an extended manual playthrough and long-duration memory soak were not performed.
