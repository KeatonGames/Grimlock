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

`game/assets/config/towers.json` defines each reusable tower type. Base (Level 1) values:

| Type ID | Display name | Cost | Damage/hit | Range | Shots/sec | Projectile speed | Projectile radius |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `basic` | Small Tower | 30 | 25 | 3.5 | 1.5 | 9 | 0.06 |
| `wide` | Large Tower | 50 | 60 | 5 | 2 | 12 | 0.10 |

Each type stores `construction_cost`, `projectile_speed`, `projectile_radius`, and
RGBA `projectile_tint`. Its ordered `levels` entries hold `damage`, `attack_range`,
`fire_rate`, and `upgrade_cost`. See [tower upgrades](tower_upgrades.md) for the
three authored levels, selection controls, runtime ownership and transactions.
Combat values must be finite and positive; `fire_rate` supports up to 60 shots/sec.
Ranges use horizontal world distance from tower center to enemy foot position.
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
  It selects the nearest living in-range enemy, with stable IDs breaking ties.
  Removed/disabled towers lose attack state on synchronization. Already-fired
  projectiles can finish independently of their source tower.
- `ProjectileSystem` owns visible runtime cube instances sharing the existing cube
  model/GPU resources. Projectiles home toward the target's body and sweep their
  whole movement segment to prevent tunneling. Missing/dead targets invalidate
  projectiles; they never retain pointers into packed enemy storage. Projectiles
  do not introduce area damage, physics, or line-of-sight/wall collision rules.
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
simulation averaged about 2.9 ms per tick on the development machine (simulation
only; Debug averaged about 19 ms). These are measurements, not frame-rate guarantees.

Full Game/Editor builds passed in Debug and Release, with 31/31 CTest checks passing
in each configuration. The expanded mixed battle check was also rerun in Release.
Vulkan validation reported no errors or synchronization hazards; formatting and
whitespace checks passed. Phase 26 build/test logs are in `out/phase26/`.
