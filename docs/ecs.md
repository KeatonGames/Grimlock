# Incremental ECS

`engine/include/grimlock/ecs.hpp` is a standalone C++20 registry. It has no renderer,
navigation, gameplay, serialization, scheduler, or third-party ECS dependency.

## Storage and identity

- `ecs::Entity` is a registry-scoped 64-bit handle: a 32-bit index-plus-one and a
  32-bit generation. Zero is null. Destroyed slots are reused with a new generation;
  exhausted generations retire their slot rather than wrapping. Handles must be
  resolved through their owning registry, not transferred between registries.
- Each component type has one sparse-set pool: contiguous values and entity handles,
  plus a sparse index. Lookup and swap removal take constant time. Pools retain
  capacity; component vectors grow geometrically rather than allocating per entity.
- The registry owns components and invokes their normal destructors. Components
  must support nonthrowing move construction, move assignment, and destruction so
  packed removal and exception cleanup remain safe. Component construction may throw.
- Type-erased ownership is confined to the pool map. Component lookup and views
  remain typed; no per-entity virtual dispatch or heap object is needed.
- `clear()` destroys live entities without resetting generations. It retains vector
  capacities for reuse. `capacityBytes()` reports these capacities, excluding map
  overhead, component-owned allocations such as paths, and external GPU resources.

## Mutation and views

```cpp
ecs::Registry registry;
const auto entity = registry.create();
registry.emplace<Position>(entity, Position{1, 2});
registry.emplace<Health>(entity, Health{100});
registry.each<Position, Health>([&](ecs::Entity current, Position& position, Health& health) {
    position.x += 1;
    if (health.value <= 0) {
        registry.destroy(current);
    }
});
```

`get<T>()` returns null for missing components and stale handles. Const registry
access returns const data. Views walk the smallest requested pool and join the
others by sparse lookup. Callback references remain valid until the outermost
view exits, including nested views.

Destruction inside a view invalidates the handle immediately and defers storage
compaction. Component removal becomes visible when the outermost view exits.
Commands commit in FIFO order, even while unwinding a throwing callback. Component
insertion, reserving, and clearing are rejected during views, because they could
relocate callback references. Create IDs and attach components between views when
spawning. Outside views, removals commit immediately; pointers and packed spans
must not survive a structural mutation.

## Gameplay integration

- `EnemySystem` owns a registry containing `EnemyRuntimeComponent` and
  `TeamComponent`. The existing `Enemy` name is a compatibility alias, and
  `enemies()` borrows the registry's packed data. There is no mirrored legacy
  enemy vector or ID lookup map. Hit feedback uses a typed component join.
- Enemy runtime fields remain grouped for this incremental migration. This keeps
  the existing span-based crowd and AI algorithms intact instead of introducing
  copies or pointer-heavy adapters for every field. No unused transform/health/
  navigation component shells were added merely to expand the component catalog.
- Enemy references use generation-checked handles. `spawnOrder` separately preserves
  the old deterministic ordering for nearest-target ties, crowd priority, melee
  approach staggering, and animation phases. Recycled handle bits never determine
  gameplay priority. The component's `id` is a compatibility copy of its owning
  handle; systems must not change it.
- `ProjectileSystem` owns a separate registry containing `ProjectileComponent`
  and `ProjectileLifetimeComponent`. The existing projectile span also borrows
  its ECS pool. Position, homing velocity, speed, damage, source, target, and render
  instance live in the projectile component. Lifetime is independently stored;
  normal shots retain their existing unlimited lifetime while the target is valid.
  The optional trailing `spawn()` lifetime argument permits explicit expiry.
- Existing indexed movement/projectile loops keep immediate swap removal and
  processing order. Generic views provide deferred removal when a system needs it;
  applying it indiscriminately to these loops would change combat ordering.
- Game systems release Engine instances before destroying their entities. Spawn
  failures roll back partial components and render instances. Death removes the
  enemy entity immediately; the existing render-only death effect owns its
  remaining animation instance. ECS components never own Vulkan or Detour objects.
- Engine instance IDs remain generation-safe render handles. NavMesh references
  remain borrowed from the owning navigation session, whose stable profiles outlive
  its enemies; paths still validate mesh revisions, and shutdown clears actors
  before releasing profiles. Runtime handles are never serialized into level JSON.

Heroes, towers, static scene objects, resource caches, and Editor documents retain
their existing ownership. Enemy/projectile registries are private; public diagnostic
access is const so callers cannot bypass resource cleanup through a raw destroy.
Restart and level switching use the existing system cleanup boundaries.

## Update order

The existing single-threaded application orchestration remains explicit. For each
bounded Game battle step:

1. Synchronize navigation and geometry; prepare enemy targeting and decisions.
2. Advance enemy AI/navigation/crowd movement, effects, and model transforms.
3. Resolve enemy attack impacts and objective arrivals; remove consumed entities.
4. Refresh geometry after tower destruction and update tower effects.
5. Run tower targeting, firing, and swept projectile impacts.
6. Advance the authored wave schedule, spawning, rewards, and phase transitions.
7. Synchronize heroes, then run hero combat and movement.
8. After runtime updates, Engine evaluates animations/lights and renders.

No event bus or ECS scheduler duplicates this order. Structural changes happen at
the existing loop barriers or at the end of an explicit component view.

## Checks and remaining scope

`Ecs.Core` covers creation, generation reuse, sparse lookup/removal, component joins,
owned-resource cleanup, constructor failure, nested views, skipped destroyed
entities, deferred component removal, callback failure, and insertion guards.
`Ecs.Runtime` covers real enemy/projectile components, stale recycled targets,
simultaneous impacts, expiry, spawn ordering, reset, and render cleanup.

The existing gameplay checks continue to cover heroes, towers/upgrades, bosses,
terrain, LOS, economy, waves, level switching, and Editor controls. The combat
stress executable supports `--large` to add a fixed 500-enemy scenario; see the
performance results below. `--animated` retains the mixed animated variant.

Further migration should first measure whether finer enemy movement/health/combat
pools improve useful iteration. Extract one responsibility with a direct view of
its data rather than copying it into a second representation. Migrating heroes or
towers, or consolidating registries into a shared game world, is future work; none
is required by this phase's lifetime or update order.

## Performance observations

Build and run from the repository root:

```powershell
cmake --build --preset release --parallel 4
ctest --test-dir out/build/release -C Release -R '^Ecs\.' -V --output-on-failure
.\out\build\release\Release\CombatStressChecks.exe 180 --large
.\out\build\release\Release\CombatStressChecks.exe 180 --animated
```

On this Windows x64 development machine, three alternating pre-ECS/ECS Release
trials used the same 960x540 scenario and 180 measured frames after 30 warm-up
frames. Validation was disabled for timing. Each scenario included eight towers,
three heroes, mixed enemy types, one boss, and a fixed 128-projectile stress volley.
The 500-enemy case uses a wider predefined spawn rectangle. The saved pre-ECS
executable used the same harness, assets, and simulation settings.

Numbers average the three per-trial results; percentiles average per-trial
percentiles rather than pooling frames. Logs are in
`out/phase35/{baseline,ecs}-{1,2,3}.log` in this workspace.

| Enemies | Frame mean before/after (ms) | FPS before/after | p95 before/after (ms) | p99 before/after (ms) | Movement CPU before/after (ms) |
| --- | --- | --- | --- | --- | --- |
| 100 | 4.134 / 4.138 | 241.915 / 241.667 | 4.597 / 4.710 | 4.760 / 4.880 | 0.383 / 0.371 |
| 200 | 4.112 / 4.106 | 243.187 / 243.553 | 4.582 / 4.617 | 4.738 / 4.783 | 0.762 / 0.792 |
| 500 | 5.785 / 5.688 | 173.033 / 175.877 | 4.393 / 4.540 | 11.794 / 12.195 | 2.431 / 2.295 |

This migration does not establish a performance improvement. At 500 enemies,
AI was 1.904/1.805 ms, hero combat 0.011/0.011 ms, tower combat/projectiles
0.024/0.023 ms, and GPU 1.182/1.193 ms. The mean of trial maximum frames was
176.492/164.426 ms: the pre-existing large-crowd navigation/targeting spikes remain.
Presentation limits the smaller scenarios, and short-run scheduling/GPU-clock
variation is visible. The basic ECS benchmark takes approximately 7 ms for 100,000
compound create/add/join/update/deferred-destroy operations with 40,000 bytes of
retained vector capacity; it is not an isolated iteration benchmark or a speedup claim.

At 500 enemies, process private memory was 1812.630/1820.180 MiB and working set
1371.988/1385.781 MiB. Tracked ECS vector capacities for the enemy and both projectile
registries were 393,888 bytes after migration. This includes component data and
retained metadata but excludes component-owned paths, allocator/map overhead, and
GPU resources. The process figures do not demonstrate a memory reduction or prove
long-session leak freedom.

The final trials matched printed impacts, defeats, survivors, and position/health
checksums at every population. The 500-enemy runs recorded 130 peak projectiles,
86 impacts, four defeats, and 492 survivors on both sides. The animated 200-enemy
variant also completed. No prolonged interactive soak, external heap profiler,
sanitizer run, or GPU capture was performed.

## Verification results

Game and Editor build successfully with both CMake presets. Full CTest runs passed
41/41 in Release (151.70 s) and 41/41 in Debug (303.67 s), with Vulkan validation and
synchronization validation enabled and no reported validation errors. These runs
include the ECS tests, gameplay balance, Cleave, bosses, 500/508-agent checks,
navigation, terrain, animations, economy, progression, level switching, and Editor
controls. All modified C++ files pass root clang-format verification, and
`git diff --check` passes.
