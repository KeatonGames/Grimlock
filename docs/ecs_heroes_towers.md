# ECS heroes and towers

Phase 36 extends the existing registry; it does not add a second ECS or change the
game's simulation order. Enemies, projectiles, heroes, and towers now use the same
generation-checked identity and sparse-set storage implementation.

## Ownership and components

| Owner | ECS data |
| --- | --- |
| `HeroSystem` | `HeroRuntimeComponent` (`Hero` compatibility alias), player `TeamComponent` |
| `TowerSystem` | `Tower` body, `TowerProgress`, `TowerAttack`, player `TeamComponent` |
| `EnemySystem` | Existing enemy runtime component and enemy team |
| `ProjectileSystem` | Existing projectile and lifetime components |

The hero vector, monotonic hero ID counter, tower vector, tower progress map, and
CombatSystem's attack-state vector were removed. Packed spans borrow ECS data;
there is no second authoritative runtime representation. Definitions remain in
their existing data modules and JSON configuration.

`HealthComponent` groups health, maximum health, and hit feedback for heroes,
enemies, and tower progress. It is an embedded data-only base that preserves the
existing flat field API, not a polymorphic actor hierarchy or a separately
registered health pool. Damage uses the existing saturating helper through the
same health overload. Team/hostility checks use ECS team data. Target validation
resolves generation handles through the appropriate actor owner.

Movement, navigation, targets, and specialized combat remain grouped in their
existing runtime records. Hero manual overrides, Cleave, animations, enemy AI,
stationary tower fire, and projectile sweeps keep their distinct systems. Tower
cooldowns retain the existing cadence accumulator; they are not forced into a
generic clamped timer that would drop shots on slow frames.

## Scene and navigation bridge

- Scene object IDs remain authoring, rendering, picking, and serialization handles.
  `TowerSystem::entityForObject()` translates that boundary into runtime identity.
  The map stores only the bridge, not health or combat state.
- Tower body synchronization reads Scene transforms in one place. It updates ECS
  only when the source body changes. Gameplay does not independently write a
  second tower transform. The temporary Scene projection is not runtime authority.
- Runtime enemy targets and projectile attacker attribution reference tower ECS
  handles. Separate Scene handles are used solely for geometry exclusion and
  rendering. `TowerAttack` belongs to its ECS entity and no longer stores a
  redundant Scene ownership ID. Debug readers use `TowerSystem::attacks()`.
- Registry sorting preserves the previous Scene-ID tower processing order after
  swap removal. Sorting is forbidden inside active component views, preserves
  component identity, and only allocates a permutation when a reorder is needed.
- Disabling a tower removes its active body and attack component while retaining
  its progress and identity. Re-enabling restores its health/level, matching the
  previous behavior. Deleted objects or objects losing their tower role destroy
  the runtime entity and all components.
- Tower destruction removes the Scene object, invalidates its ECS handle, and
  changes the obstacle revision. Existing navigation synchronization batches the
  Recast rebuild after impacts. Footprints remain independent of model scale.

Hero identity survives repositioning. A separate spawn sequence preserves melee
approach staggering independently of recycled handle bits. Hero death retains
the existing renderable dead body and stops movement/combat; `remove()` or reset
releases the Scene object and entity. Synchronization also removes runtime heroes
whose Scene objects were deleted externally.
Lookup and commands reject missing Scene objects immediately, before that next
synchronization, so combat and UI cannot dereference an already removed model.

## Selection, lifetime, and compatibility

Hero selection uses a generation handle. Tower gameplay selection now also stores
an ECS handle; Scene picking converts it once, and subsequent UI reads resolve the
current component. Removed actors cannot be rebound accidentally to recycled slots.
The existing UI, range indicators, construction/upgrade costs, and preparation
rules continue to use their current public interfaces.

Registries are still scoped to their systems. `DefensiveTargetKind` identifies the
owner for combat references; handles are not globally interchangeable. Public
registry access is const so callers cannot bypass Scene/resource cleanup.
Renderer assets and Detour profiles keep their existing owners. Level files never
contain runtime generations, health, cooldowns, or runtime upgrade changes.
No static Editor objects were migrated and no level format changed.

The update order remains enemy decisions → movement → enemy impacts → obstacle
synchronization → tower/projectile combat → waves → hero combat/movement → Engine
animation/render synchronization. Existing substeps and safe mutation barriers
remain in use; no scheduler, event bus, jobs, or threads were introduced.

## Validation

The extended `Ecs.Core` verifies that sorting preserves sparse lookup and identity.
`Ecs.Runtime` now also checks hero creation/removal/reuse, stale commands,
repositioning, movement, tower reuse/order, independent upgrades, damage,
disable/re-enable state, navigation invalidation, hero/tower selection removal,
and level restart. Existing combat, terrain, boss, economy, UI, wave, and Editor
checks cover the preserved gameplay behavior.

The remaining architectural boundary is deliberate: actor records are still
coarsely grouped and each owner has its own registry. Finer health/transform/
movement pools or a shared game-world registry should be considered only with a
clear consumer and lifetime plan. Static scenes and GPU resource management
remain outside this migration.

## Performance comparison

Three alternating Phase 35/current Release trials used the same saved baseline
executable, assets, 960x540 window, 30 warm-up frames, and 180 measured frames per
population. Each battle used eight towers, three heroes, mixed enemies, one boss,
and a fixed 128-projectile volley. Validation was disabled for timing. Run the
current harness with `CombatStressChecks.exe 180 --large`; logs are under
`out/archive/phase36/{baseline,ecs}-{1,2,3}.log` in this workspace.

Values average three per-trial results; percentile values average per-trial
percentiles rather than pooling all frames.

| Enemies | Frame mean before/after (ms) | FPS before/after | p95 before/after (ms) | p99 before/after (ms) |
| --- | --- | --- | --- | --- |
| 100 | 4.142 / 4.139 | 241.421 / 241.603 | 5.008 / 4.799 | 5.174 / 5.046 |
| 200 | 4.117 / 4.109 | 242.923 / 243.402 | 5.025 / 4.932 | 5.190 / 5.166 |
| 500 | 5.571 / 5.722 | 179.497 / 174.858 | 4.869 / 5.113 | 13.020 / 12.511 |

No performance gain is claimed. At 200 enemies, AI/synchronization CPU was
0.030/0.040 ms, movement 0.773/0.820 ms, hero combat 0.005/0.006 ms, and tower
combat/projectiles 0.017/0.018 ms. GPU time was 1.340/1.165 ms; no rendering
optimization was introduced to explain that clock/scheduling variation.

The 500-enemy mean regressed by about 2.7%; the average trial maximum was
151.753/165.698 ms. The existing large-crowd navigation/targeting spikes remain,
and the new identity/Scene/team validation adds some CPU work. These short
measurements do not isolate the contribution of each cost or scheduling noise.
The 100–200-enemy mean remains effectively presentation-limited.

At 200 enemies, process private memory was 1800.827/1812.375 MiB and working set
1393.322/1393.419 MiB. Current tracked ECS vector capacity was 238,232 bytes,
including the new hero/tower registries; the baseline's 229,024-byte ECS figure
excluded its legacy hero/tower containers and is not a like-for-like memory total.
No memory reduction or long-session leak freedom is claimed.

Impacts, defeat/survivor counts, and printed enemy position/health checksums matched
across all measured populations, including 500. Resource sharing, animations,
terrain, and gameplay correctness are additionally covered by the runtime suites.
No external heap/GPU capture, sanitizer run, or prolonged interactive soak was performed.

## Verification results

Game and Editor build in Release and Debug. Full CTest runs passed 41/41 in Release
(149.92 s) and 41/41 in Debug (294.02 s), with Vulkan validation and synchronization
validation enabled and no reported validation errors. After the final missing-Scene
hero guard, both builds passed the focused ECS, hero, battle, and Game runtime
checks again (4/4 each: 17.41 s Release, 34.94 s Debug). Root clang-format verification
and `git diff --check` pass. The authored level and scene formats were not changed.
