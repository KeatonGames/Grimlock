# Performance and stability investigation

Phase 40 compares Release builds against commit `0a224d7`. Camera bindings,
combat statistics, targeting rules, navigation algorithms, and Vulkan resource
ownership are preserved.

## Findings and fixes

- Game hover highlighting called exact scene picking every world-pointer frame.
  Picking skinned every vertex before rejecting a ray against deformed bounds,
  including static bind-pose previews. Three deployed heroes made this work
  visible in the HUD/input portion of the frame, while the previous combat-only
  benchmark never exercised that path. Shared picking geometry now stores bounds
  of vertices influenced by each joint. Transforming those bounds rejects clear
  misses without deforming the full mesh. Candidate hits still use the original
  exact triangles, material overrides, transparency, clipping, and current pose.
  Morphing and non-convex skin weights retain the exact fallback. The bounds are
  recomputed from the current pose on each query, with no per-frame allocation or
  cross-instance pose cache. A selection click also reuses its result for hover.
- The crowd spatial hash retained abandoned cells as occupied buckets after
  relocation. Repeated relocations could exhaust its probe table and loop forever.
  Empty buckets are now removed and their probe chains repaired. Linked neighbor
  lists, query order by cell, collision rules, and movement integration stay intact.
  A colliding-cell regression moves four agents through 1,024 distinct positions
  without rebuilding the hash and checks surviving cells after every move.
- Engine wall-clock deltas were unbounded. The outer gameplay loops split a long
  stall into many updates, bypassing the crowd controller's existing recovery cap.
  Engine frames now pass at most 0.25 seconds to simulation and animation after a
  debugger pause, window drag, or synchronous load. Excess stalled time is discarded;
  normal frame deltas and explicit simulation/test updates are unchanged. This is
  a recovery policy, not a change to attack cadence or navigation substeps.
- Worst-frame instrumentation identified the sustained crowd spikes as required
  navigation rebuilds following tower destruction. The grid adapter supplied every
  floor cube's internal faces to Recast. Navigation geometry now coalesces touching
  coplanar floor boxes along rows, retaining gaps, height changes and walls. A 40x32
  floor supplies 384 triangles instead of 15,360. Rendering is unchanged. Tests
  compare merged/unmerged clearance at half-cell samples with 0.5, 1 and 1.3 cell
  sizes, test detours, and reject merging gaps, elevation changes and walls.

## Audit scope

Reviewed the Game/Editor update entry points, level and wave transitions, hero and
enemy AI, targeting and melee navigation, crowd contact queries, projectiles,
terrain/combat geometry synchronization, ECS storage and deferred lifetime rules,
scene picking and animation, resource caching, deformation uploads, and Vulkan
frame preparation/synchronization. Existing synchronization calls serve movement,
spawn, and tower-destruction boundaries; they are not duplicate simulation updates.
No demonstrated regression justified replacing the renderer, introducing instancing,
changing path algorithms, or throttling AI/animations.

The existing combat benchmark reports AI, movement/navigation, hero combat, tower
combat plus projectiles, animation, preparation, render, GPU time, allocations,
memory, and entity cleanup. The new gameplay benchmark adds the actual Game HUD,
three hero types, both tower types, pointer-over-UI versus pointer-over-world,
empty/preparation/battle frames, and repeated level load/unload cycles. This closes
the missing hover-picking coverage.

## Reproduction

Build using the README presets, then run one graphical test at a time:

```powershell
$env:VK_INSTANCE_LAYERS = ''
& out/build/release/Release/GameplayStressChecks.exe 180 --profile
& out/build/release/Release/CombatStressChecks.exe 180
& out/build/release/Release/CombatStressChecks.exe 180 --animated
& out/build/release/Release/CombatStressChecks.exe 720 --large
```

`Game.Stress` runs a shorter 30-sample check, including a deliberate 300 ms stall
that verifies the recovery budget. `--profile` skips that assertion/delay so the
same harness can measure an older baseline. Each scenario has 30 warm-up frames;
simulation uses a fixed 1/60 step. `PICK_MISS_MS` measures 100 identical scene queries
with three heroes. Game HUD trials require an available desktop/display and verify
that the requested UI/world pointer path actually ran.

`GAMEPLAY` CSV rows report FPS, mean/p95/p99/maximum frame times, game CPU,
UI/events CPU, preparation/animation/render CPU, GPU mean, Windows private MiB,
C++ allocation calls, and retained instance/model counts. UI/events is the residual
frame time outside Game/preparation/render, including polling and camera input.
Animation is part of preparation. Render includes presentation/fence waits, so it
must not be interpreted as pure draw-submission cost. Process memory and intercepted
`new` calls do not establish native/GPU leak freedom.

Raw investigation, build, test, and comparison logs are under ignored `out/phase40`.

## Measurements

Three alternating before/after Release trials used 180 measured frames per case,
960x540, validation disabled, with graphical runs sequential and no concurrent
build. Each Game HUD trial loads and unloads First Watch three times. Values below
average the nine phase samples on each side; percentiles average each sample's
percentile rather than pooling individual frames. These trials isolate the picking,
hash and recovery changes, before the final floor-input optimization.

| Game scenario | UI/events ms before / after | Mean frame ms before / after | p99 ms before / after |
| --- | --- | --- | --- |
| Empty | 0.017 / 0.017 | 4.166 / 4.166 | 4.863 / 4.862 |
| Preparation, pointer over UI | 0.042 / 0.043 | 4.216 / 4.179 | 6.088 / 5.461 |
| Preparation, pointer over world | 3.489 / 0.098 | 4.172 / 4.170 | 5.627 / 5.861 |
| Battle, pointer over world | 3.696 / 0.097 | 4.297 / 4.166 | 6.364 / 5.433 |

The identical three-hero miss query averaged 3.479 / 0.050 ms (about 98.6% less CPU
work). Presentation near 240 Hz masks much of that improvement in mean FPS.
Preparation's p99 did not improve consistently; remaining scheduling/presentation
noise must not be attributed to the picking change.

The existing 50/100/200 mixed-enemy trials averaged 4.156/4.158, 4.140/4.140 and
4.114/4.112 ms before/after. At 200 enemies, CPU means were AI 0.028/0.029,
movement 0.766/0.794, heroes 0.006/0.006, tower combat/projectiles 0.017/0.017,
animation 0.012/0.012 and GPU 1.421/1.336 ms. These stages were not materially
improved by the picking fix. All nine paired combat cases matched impacts,
defeats, survivors, position/health checksums, and measured allocation counts.

An additional animated 200-enemy pair measured 2.364/2.498 ms animation CPU and
1.994/1.974 ms GPU; this is evidence of a remaining animation cost, not a claimed
improvement. The fixed volley exercises up to 130 simultaneous projectiles.

Two paired 720-frame trials then isolated floor coalescing, retaining all earlier
fixes on both sides. The worst frames occurred in navigation rebuilds, with the
`WORST` row reporting AI/synchronization, movement, heroes, combat and whether the
navigation revision changed. Every one of the eight paired combat outcomes matched.

| Enemies | Mean of trial worst-frame ms before / after |
| --- | --- |
| 50 | 147.338 / 122.309 |
| 100 | 150.461 / 131.160 |
| 200 | 152.722 / 123.832 |
| 500 | 148.579 / 127.401 |

The 500-enemy rebuild spike fell about 14%. It remains substantial. This change
does not make full Recast rebuilds asynchronous or replace them with tiled updates.
Raw CSV is in `profile-*.log`, with summaries in `comparison.json` and
`navigation-comparison.json`.

Across repeated Game loads, retained model counts stabilized at three once the
enemy model was loaded, and unload checks found zero standalone instances. Process
private memory fluctuated around 1.7–1.8 GiB after the assets warmed; animated stress
used about 2 GiB. These snapshots and unchanged frame allocation counts do not
demonstrate a reduction in retained memory or prove absence of leaks.

## Verification

- Complete final Release and Debug builds passed for Game, Editor and checks.
- Release CTest: **42/42**, 148.63 seconds.
- Debug CTest: **43/43**, 286.04 seconds, including the optional standalone ECS
  AddressSanitizer check. The sanitizer option was returned to its previous OFF
  setting afterward; the normal suite contains 42 tests.
- Both full suites used Vulkan core and synchronization validation. Their final
  logs contain no validation errors, VUID diagnostics or synchronization hazards.
- Covered hero types/manual commands, camera input, combat/targeting, bosses,
  projectiles, tower placement/upgrades, terrain/clearance, waves/economy,
  progression, level switching/restart, Game HUD and independent Editor runtime.
- All 12 modified C++ files passed root clang-format verification; whitespace and
  relative documentation-link checks passed.

Final build/test logs use `build-{release,debug}-final.log` and
`tests-{release,debug}-final.log` under `out/phase40`. Earlier investigation runs
are retained separately and are not the final verification.

## Remaining limits

Exact picking still deforms candidate skinned meshes; morph-heavy picking retains
the original path. Synchronous Recast rebuilds after obstacle changes still cause
roughly 120–140 ms spikes on the large grid fixture. Animated large crowds and the
existing single-frame Vulkan presentation model remain costs in these profiles.
Short repeated-load tests can verify cleanup and stable cache counts, but
cannot establish long-session leak freedom. There was no external GPU capture,
full-application sanitizer build, or extended manual playthrough. The standalone
ECS sanitizer does not cover Vulkan or third-party allocations.
