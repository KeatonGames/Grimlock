# Combat performance

Enable **Navigation > Show Performance** in Grimlock Editor to inspect CPU frame,
GPU, animation preparation, movement, AI, navigation, combat/projectile timings,
entity counts, cached models, model primitives, and path-query counts. Battle
timings accumulate the existing simulation substeps. The movement reading covers
the last enemy update. AI timings include scene synchronization and attack resolution;
combat timings include projectile updates. These diagnostics are disabled by default and never add a
Game HUD panel.

CPU render time includes presentation/acquisition/fence waits. Animation time is
part of preparation, not an additional frame stage, and includes checking imported
instance lights. GPU timestamps cover the preceding submitted render command
buffer, not presentation. Unsupported timestamps appear as unavailable. Results
are read after the existing single-frame fence; profiling adds no GPU wait.

## Repeatable stress test

After building Release, run from the repository root:

```powershell
.\out\build\release\Release\CombatStressChecks.exe 180
.\out\build\release\Release\CombatStressChecks.exe 180 --animated
ctest --test-dir out/build/release -C Release -R '^Combat.Stress$' --output-on-failure
```

The executable runs 50, 100, and 200 enemies at 960x540, with eight towers, three
heroes, Chicken/Grunt/Siege/Hunter types, one Iron Stag, and a fixed 128-projectile
volley in addition to normal tower attacks. It uses existing definitions without
altering gameplay statistics. Each scenario has 30 warm-up frames and the requested
number of measured frames. Simulation advances at a fixed 1/60 second per frame;
animation retains the Engine clock. Assertions run outside measured frame time.
`--animated` also includes Black Rat instances with their existing animation clips.

CSV rows report mean FPS/frame time, p50/p95/p99/maximum frame time, CPU system
means, GPU mean when supported, Windows process private/working-set MiB, peak
projectiles, impacts, defeats, survivors, a position/health checksum, and animation
time. Missing GPU readings use -1; process memory is available on Windows only.
CTest uses a shorter 60-frame run and imposes no machine-dependent speed threshold.

The test checks radius-aware navigation every frame, nearby queries against exact
full scans, stable ID lookups after packed removal/death/respawn, and zero remaining
standalone render instances after each battle cleanup. Existing runtime checks
exercise elevated terrain, passages, animations, combat, bosses, and level switching.

## Phase 34 measurements

Measured locally on Windows x64, Release, at 960x540 with validation disabled for
timing. The baseline was built with the same diagnostics and scenario before the
optimizations. Three alternating baseline/optimized trials used 180 samples each.
Values below average the three per-trial measurements; percentile columns average
the per-trial percentiles rather than pooling frames. Raw logs are in
`out/phase34/final-{baseline,optimized}-{1,2,3}.log` in this workspace.

| Enemies | Movement CPU before/after (ms) | Frame mean before/after (ms) | FPS before/after | p95 before/after (ms) | p99 before/after (ms) |
| --- | --- | --- | --- | --- | --- |
| 50 | 0.189 / 0.150 | 4.155 / 4.154 | 240.675 / 240.760 | 4.415 / 4.455 | 4.780 / 4.727 |
| 100 | 0.448 / 0.348 | 4.138 / 4.137 | 241.663 / 241.745 | 4.710 / 4.588 | 4.949 / 4.813 |
| 200 | 0.990 / 0.747 | 4.112 / 4.117 | 243.187 / 242.892 | 4.594 / 4.526 | 4.913 / 4.946 |

At 200 enemies, movement CPU fell about 25%. AI was 0.029/0.028 ms, hero combat
0.006/0.005 ms, tower combat plus projectiles 0.017/0.017 ms, preparation
0.018/0.018 ms, and GPU 0.872/0.909 ms. The mean of each trial's worst frame was
5.176/5.344 ms. Working set was 1393.456/1393.438 MiB; process private memory was
1804.968/1819.197 MiB. No memory-use or GPU-time improvement is claimed.

All nine scenarios on each side produced matching impacts, defeats, survivor
counts, and printed position/health checksums. The 200-enemy scenario had 130 peak
projectiles, 148 impacts, four defeats, and 191 survivors. This supplements the
regression tests; a checksum alone is not proof of behavioral equivalence.
The existing `Combat.Battle` check also exercises 508 mixed agents and two heroes,
while `Enemies.Runtime` renders and moves 500 chickens and checks animation playback.

The frame rate stayed near the presentation limit despite the reduced movement
work. Neither average FPS nor tail latency improved materially. GPU clocks,
scheduling, and short-run timing noise remain visible. A separate animated run
measured 2.417 ms of animation preparation with 200 enemies; animation evaluation
is a remaining CPU cost. That variant has no recorded pre-optimization comparison.

## Changes and limits

- Reuse projected Detour surface points for containment and steering visibility
  within the same operation. Tolerances, height checks, collision radii, raycasts,
  path revisions, and movement algorithms remain intact; nothing is cached across
  a NavMesh rebuild. Existing path-refresh and target-retention policies stay in use.
- Resolve stable enemy IDs through an index updated on packed removal. Projectiles,
  damage, manual commands, and AI no longer perform a full enemy scan for an ID.
  Removed IDs are erased and never reused; storage tracks live enemies rather than
  accumulating every ID ever issued.
- Reuse the existing spatial hash for shared tower/hero acquisition queries.
  Movement, spawn, death, and removal invalidate it. Exact range, elevation, LOS,
  pursuit, nearest-ID tie breaking, and target hysteresis still apply. Small or
  broad queries use the simpler full scan. Enemy scans over the much smaller
  defender population were retained because their measured cost was small.
- Reject out-of-range hero melee checks before tracing visibility.
- Retain the primitive-draw scratch vector between frames, eliminating its repeated
  allocation/growth. Existing shared meshes/materials/textures, per-instance skinning,
  draw ordering, and Vulkan synchronization remain unchanged.

No renderer redesign, animation throttling, damage approximation, new pathfinder,
or threading was introduced. Instancing was evaluated against the measurements;
this workload did not establish GPU/draw submission as the dominant limitation.
Retained asset caches and scratch capacities are intentional. Process-level memory
samples and cleanup assertions do not establish long-session leak freedom; no
external heap/GPU capture or prolonged interactive soak was performed.

## Verification

Both Game and Editor build in Release and Debug. Full CTest suites passed 39/39
in Release (138.97 s) and Debug (269.35 s), with Vulkan validation and synchronization
validation enabled and no reported validation errors. These checks include the new
stress scenario, existing 500/508-agent scenarios, elevated terrain, boss combat,
projectiles, animations, economy, progression, level transitions, Game UI, and Editor
tools. Formatting verification and `git diff --check` also pass.
After the final profiling-toggle and spawn-cleanup fixes, both configurations were
rebuilt and `Combat.Stress` plus `Enemies.Runtime` passed again (2/2 each), including
the Editor overlay enable/disable check, with no reported Vulkan validation errors.
