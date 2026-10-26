# Autonomous combat

Run `Grimlock`, build towers during preparation, then press **Start Wave**.
Towers attack automatically during Battle. Click a placed tower to inspect its
statistics and show its range; click empty terrain to clear selection. Placement
also displays the selected type's range. Camera gestures keep their existing behavior.
The Editor's Level preview uses exactly the same combat update. Free tower authoring
and navigation sandbox previews do not run combat outside an active level Battle.
Heroes automatically defend their assigned area; see [hero controls](heroes.md).
Enemies attack nearby living heroes and towers, then continue to their assigned
objective when defenders die, escape pursuit range or cannot be reached.
Damaged heroes/towers have health bars and red hit feedback; selected hero targets
retain their red ground marker. These indicators use the existing HUD overlay.

## Configuration

Grunt, Siege and Hunter definitions, targeting policies and the Rival Watch scenario
are described in [enemy archetypes](enemy_archetypes.md). Existing animal-based
levels retain their original combat statistics and targeting behavior.

`game/assets/config/towers.json` defines each reusable tower type. Base (Level 1) values:

| Type ID | Display name | Cost | Damage/hit | Range | Shots/sec | Projectile speed | Projectile radius |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `basic` | Small Tower | 30 | 25 | 3.5 | 1.5 | 9 | 0.06 |
| `wide` | Large Tower | 60 | 55 | 4.2 | 1 | 12 | 0.10 |

Each type stores `construction_cost`, `projectile_speed`, `projectile_radius`, and
RGBA `projectile_tint`. Its ordered `levels` entries hold `damage`, `attack_range`,
`fire_rate`, and `upgrade_cost`. See [tower upgrades](tower_upgrades.md) for the
three authored levels, selection controls, runtime ownership and transactions.
Combat values must be finite and positive; `fire_rate` supports up to 60 shots/sec.
Ranges use 3D world distance between terrain-level entity anchors. Visibility and
projectile sweeps run from the firing point to the target's physical body center.
Existing `half_extent`, `visual_scale`, model paths and scene role IDs are independent
of these statistics. Old catalogs without combat fields receive compatible defaults.
`maximum_health` is 250 for Small Towers and 500 for Large Towers, independent of
their combat levels/physical footprints. Upgrades do not heal towers. Runtime damage
is not saved into the authored scene, and restarting restores initial health.
Rebuild to stage asset changes, or launch with an absolute source level path as
described in [runtime applications](runtime.md).

Enemy `maximum_health` belongs in `game/assets/config/animals.json` and defaults to
100 for existing definitions. Each spawn copies it into maximum/current health.
Enemy deaths grant no gold; the existing fixed wave rewards remain unchanged.

Each animal's `combat` object defines explicit `damage`, `range`, `attack_interval`,
`impact_time`, `aggression_range`, `pursuit_range`, `attack_animation`, and integer
`hero_priority`, `tower_priority`, `objective_priority` values. Higher priority wins;
nearest physical boundary distance breaks ties. Zero excludes that category from
opportunistic acquisition. The assigned objective remains the fallback destination.
Default priorities are hero 3, tower 2, objective 1. Targets stay stable while valid;
searches run on a 0.4-second cadence and rejected pursuits wait before retrying.
Intervals are seconds (at least 1/60), contact time lies within the interval, and
all dimensions/damage must be finite and positive. Catalogs without new fields
receive compatible explicit defaults; shipped animals have authored values.

Melee range is the gap between circles or an animal circle and the tower's rotated
footprint. Shared perimeter sampling finds valid attack positions on each attacker's
existing Recast profile. Several animals can approach different sides. Combat uses
a small approach arrival tolerance instead of the normal radius-sized objective
tolerance, which could otherwise stop units outside attack range. Clear stand-off
surface rays prevent attacks through navigation obstacles. Attack animations play
once if configured/available; static assets pause movement and use timed hits.

Objective attacks preserve the existing level rule: after a melee windup, an enemy
resolves exactly one `damage_per_arrival` hit and is removed as an arrival. This
keeps authored objective health, wave accounting and fixed rewards compatible.

## Shared implementation and lifetime

- `CombatSystem` owns per-tower cooldown/target state keyed by scene object ID.
  It selects the nearest living, visible in-range enemy, with stable IDs breaking ties.
  Near-equal targets remain stable; searches run every 0.15 seconds. Obstruction,
  removal, enemy-count changes, geometry edits and upgrades invalidate the relevant checks immediately.
  Removed/disabled towers lose attack state on synchronization. Already-fired
  projectiles can finish independently of their source tower.
- `ProjectileSystem` owns visible runtime cube instances sharing the existing cube
  model/GPU resources. Projectiles home toward the target's body and sweep their
  whole movement segment to prevent tunneling. Missing/dead targets invalidate
  projectiles; they never retain pointers into packed enemy storage. Projectiles
  stop on solid geometry before applying damage. Collision sweeps cover the whole
  frame displacement, including projectile radius; thin walls cannot be skipped on slow frames.
- `EnemySystem::applyDamage` handles health, a brief red hit flash, and death.
  Lethal hits immediately remove the enemy from movement, neighbors and targeting.
  A render-only instance plays an available `death`/`die` clip once, or briefly
  retains a tinted stationary pose. Its instance is released when the effect ends.
  Deaths and objective arrivals have separate counters.
- `LevelSession` advances navigation, combat and waves together in bounded steps
  on slow frames. Only living enemies count toward wave completion. The last
  defeat can complete a wave while its visual death effect finishes; objective
  damage applies only to arrivals. Battle termination clears remaining projectiles.
  Effects finish during preparation/results, and level restart clears all combat state.
- `EnemyCombatSystem` selects stable defensive references, prepares movement orders,
  and resolves timed impacts. It shares the same GameLogic/LevelSession code in Game
  and Editor previews. It never writes unit positions. `HeroCombatSystem` remains
  responsible for hero decisions and manual-command priority.

- Shared saturating health subtraction validates damage for enemies, heroes, towers
  and the integer-health objective. Lethal tower hits remove scene/target state;
  navigation synchronization batches the resulting obstacle rebuilds after impacts.
  Dead hero IDs remain inspectable but cannot act or block deployment. Missing/dead
  references are checked again at impact time; all swings apply damage once.

All gameplay code is in GameLogic, with no Editor or ImGui dependency. Player
selection/range feedback uses the existing Game HUD projection overlay; it shares
the normal overlay limitation that indicators are not depth-occluded.

## Terrain, cover and visibility

GameLogic's `CombatGeometry` reuses the cached level geometry. A median-split BVH
accelerates static triangle queries; dynamic towers use their upright oriented
collision footprints. NavigationSession shares this geometry with battle systems
and heroes, while standalone combat checks can synchronize their own cache.
Movement, rendering and AI remain separate.

- `grimlock.terrain` contributes actual transformed static mesh triangles.
- `grimlock.obstacle` contributes actual transformed triangles, including thin walls.
- `grimlock.decoration` never obstructs combat. Materials/opacity do not decide solidity.
- Untagged legacy props retain conservative box blockers; thin floor markers remain decoration.
- Debug-grid walls/floors retain their existing cube geometry. Constructed towers
  update dynamic blockers on placement, deletion, disabling or destruction.

Set these classifications in the existing Inspector's **Level geometry** dropdown.
Imported node/hierarchy transforms are preserved. Static mesh edits rebuild the
triangle BVH; moving actors do not. No terrain grid is needed for imported maps.

Visibility traces use tower firing points and target body centers. The attacking
tower is ignored, and melee traces ignore the target tower's own footprint.
Heroes, enemies and projectiles do not falsely obstruct their own rays.
Target scans test projectile clearance as well as center-line visibility.
Projectiles recheck each swept segment against current geometry, so a newly placed
obstacle can stop an already-fired projectile. Targets that disappear cancel their
projectiles safely. Damage, fire rates, upgrades and projectile visuals are unchanged.

Melee still requires a navigable contact position on the attacker's existing
Recast profile. Approach samples must also have a clear combat ray; damage checks
repeat the ray at impact. Inaccessible pursuits retain the existing bounded retries
and rejection delays. Cleave uses the same contact/visibility guard as ordinary
hero attacks. There are no height-based damage/range bonuses.

## Cover Watch and Editor debugging

Run `Grimlock.exe assets/levels/cover_watch.json`, or choose
**Navigation > Level > Load Cover Watch** in Editor. This handcrafted extension of
Hill Watch adds a solid mountain ridge, an authored hilltop Large Tower, lower
animal approaches and a hero defending the elevated area. The north-facing valley
has cover; the southern ramp lane stays exposed. The two waves use existing types
and explicitly authored counts/timing, with no new progression rules.

Enable **Navigation > Show Combat Visibility** and select a tower to display its
3D range sphere and potential firing rays. Green rays are clear, red rays are
obstructed, and yellow crosses mark first contact. Inspecting an animal shows rays
to defensive targets; selected hero scene objects show rays to animals. This is
an optional Editor overlay, disabled by default and absent from normal Game HUD.

`Combat.Visibility` checks BVH rays, radius/edge sweeps, thin-wall tunneling,
decoration, dynamic tower removal, target stability/reacquisition, upgrades via the
existing combat checks, and the covered elevated battle. The Editor runtime check
also exercises the visibility toggle, 3D sphere and target rays. Existing tests
continue to cover commands, wave rewards, economy, navigation and resource cleanup.
## Verification

`Combat.Runtime` checks nearest targets, range exclusion/reacquisition, simultaneous
Small/Large Towers, visible projectile movement, low-frame-rate impacts, stale
target IDs, health/death cleanup, available death animation playback and identical
stationary-target results at 1/5/30/60 FPS. `Game.Runtime` exercises real HUD
construction/selection and combat-driven victory without Editor libraries.
`Enemies.Runtime` verifies combat in Editor's Level preview, exact authored spawn
totals and rewards, objective arrivals, navigation, restart and free authoring.
Catalog checks cover invalid combat values, legacy defaults and animal health.

`Combat.Battle` covers autonomous melee exchanges, hero death, tower destruction and
navigation restoration, unreachable defenders, command priority/recovery, multiple
attackers and combat with 508 mixed animals plus two active heroes. The Release
simulation averaged about 2.36 ms per tick on the development machine (simulation
only; Debug averaged about 19.06 ms). The BVH fixture completed 20,000 rays against
10,000 triangles in about 7.4 ms in Release. These are measurements, not frame-rate guarantees.

Full Game/Editor builds passed in Debug and Release, with 34/34 CTest checks passing
in each configuration (Release 106.79 s, Debug 231.03 s).
Vulkan validation reported no errors or synchronization hazards; formatting and
whitespace checks passed. Phase 28 build/test logs are in `out/phase28/`.
