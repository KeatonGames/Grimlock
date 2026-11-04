# Gameplay ECS

`engine/include/grimlock/ecs.hpp` is a standalone C++20 registry. GameCore owns gameplay systems; the registry knows nothing about Vulkan, navigation, or ImGui. See the [validation report](ecs_validation.md) for measurements and [hero/tower migration](ecs_heroes_towers.md) for historical details.

## Identity, storage, and ownership

- `ecs::Entity` is a registry-scoped 64-bit index-plus-one/generation handle. Zero is null. Destruction advances the generation before slot reuse; exhausted generations retire their slot. `clear()` preserves generation safety.
- Each component type has a sparse-set pool: contiguous components and entities, plus a sparse lookup index. Removal swaps the last component into the removed slot. Registries and pools cannot be copied or moved.
- Components must support nonthrowing move construction, move assignment, and destruction. Construction may throw. Prefer data and RAII members; constructors/destructors should not reenter their registry.
- Component pointers, references, and spans are borrowed. Addition, removal, sorting, and clearing can invalidate them. Retain entity handles across updates and resolve again. A typed `Storage<T>&` remains stable for its registry's lifetime, including across `clear()`; component addresses do not.
- `Registry::get` checks logical validity, including pending destruction. Cached pool `get` checks committed generation-matching storage; do not use it to bypass pending destruction inside views.

| Owner | Authoritative ECS data |
| --- | --- |
| `EnemySystem` | Enemy runtime, team |
| `HeroSystem` | Hero runtime, team |
| `TowerSystem` | Tower body, progress/health/level, attack state, team |
| `ProjectileSystem` | Projectile runtime |

Health is shared data embedded in actor records, not an independently queryable pool. Definitions are immutable configuration. Render instance IDs and navigation handles borrow resources owned by Engine/navigation. Systems release resources before clearing actor records; there is no duplicate authoritative legacy actor vector.

Handles are not globally unique across owners. Combat target kind identifies the owner before resolving its handle. Scene object IDs are a separate domain: towers mirror authored Scene transforms; moving actors publish runtime transforms to rendering. Disabling a tower removes its active body/attack state but retains identity and progress. Deleting its source object removes the entity.

## Mutation and execution

`each<A, B>` walks the smallest pool and joins matching entities. Destroy requests inside views invalidate entities immediately; structural removals commit in FIFO order at the outermost view exit, including exception unwinding. Component removal is deferred. Insertion, reserve, sorting, and clear are rejected during views. Attach new components between views. Failed deferred-command allocation leaves the requested mutation unapplied.

The existing single-threaded `LevelRuntime` sequence is preserved:

1. Bound gameplay delta time; synchronize navigation and prepare enemy AI.
2. Update crowd movement, enemy state/animation/effects; resolve enemy combat.
3. Synchronize tower geometry/effects; update tower targeting and projectiles.
4. Advance handcrafted waves and rewards.
5. Synchronize heroes; update hero combat and movement.
6. Engine updates animations, lights, and rendering.

Existing bounded combat/movement substeps remain in their systems. Subsequent consumers resolve target handles again after death/removal. Stable spawn order and Scene-ID tower order preserve tie-breaking independently of reusable entity slots. Level teardown clears actors, projectiles, and selections before releasing navigation profiles. Engine retains shared GPU/model caches.

## Adding components and systems

Put reusable storage in Engine and gameplay data/behavior in GameCore. For example:

```cpp
struct Position {
    float x = 0, z = 0;
};

struct Velocity {
    float x = 0, z = 0;
};

grimlock::ecs::Registry world;
const auto entity = world.create();
world.emplace<Position>(entity);
world.emplace<Velocity>(entity, Velocity{1, 0});

const float deltaTime = 1.0F / 60;
world.each<Position, Velocity>([&](auto, Position& position, const Velocity& velocity) {
    position.x += velocity.x * deltaTime;
    position.z += velocity.z * deltaTime;
});
```

The owning system creates/destroys records and releases external resources. Keep update functions explicit and components primarily data. Add the system call at an appropriate lifecycle boundary rather than introducing a scheduler. Query existing components instead of mirroring authoritative values. Cache typed pools for measured hot lookups, not component pointers; owner `team(id)` accessors are an example. Add tests for stale handles, cleanup, and integration order. Format modified C++ and run README build/CTest commands.

## Tradeoffs and checks

The design deliberately keeps coarse actor records, per-owner registries, retained vector capacity, and existing navigation/combat algorithms. It is not a global ECS, archetype store, serializer, or thread-safe framework. `capacityBytes()` excludes map overhead, component-owned allocations, and external resources.

`Ecs.Core` checks storage/lifetimes and deterministic mutation histories; `Ecs.Runtime` checks real actor ownership, allocation failures, selections, and repeated level loads. `Combat.Stress` exercises gameplay/render paths.

The optional `GRIMLOCK_BUILD_ECS_SANITIZER_CHECKS=ON` CMake option adds standalone `Ecs.Sanitized` with AddressSanitizer. Build `EcsSanitizerChecks`, then run `ctest --test-dir out/build/release -C Release -R Ecs.Sanitized --output-on-failure`. On Windows, put the compiler's ASan runtime DLL directory on `PATH`. This checks the core registry, not the entire Vulkan application or native allocation leaks.
