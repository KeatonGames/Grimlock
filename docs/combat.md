# Tower and enemy combat

Run `Grimlock`, build towers during preparation, then press **Start Wave**.
Towers attack automatically during Battle. Click a placed tower to inspect its
statistics and show its range; click empty terrain to clear selection. Placement
also displays the selected type's range. Camera gestures keep their existing behavior.
The Editor's Level preview uses exactly the same combat update. Free tower authoring
and navigation sandbox previews do not run combat outside an active level Battle.

## Configuration

`game/assets/config/towers.json` defines each reusable tower type:

| Type ID | Display name | Cost | Damage/hit | Range | Shots/sec | Projectile speed | Projectile radius |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `basic` | Small Tower | 30 | 25 | 3.5 | 1.5 | 9 | 0.06 |
| `wide` | Large Tower | 50 | 60 | 5 | 2 | 12 | 0.10 |

Fields are `construction_cost`, `damage`, `attack_range`, `fire_rate`,
`projectile_speed`, `projectile_radius`, and RGBA `projectile_tint`.
Combat values must be finite and positive; `fire_rate` supports up to 60 shots/sec.
Ranges use horizontal world distance from tower center to enemy foot position.
Existing `half_extent`, `visual_scale`, model paths and scene role IDs are independent
of these statistics. Old catalogs without combat fields receive compatible defaults.
Rebuild to stage asset changes, or launch with an absolute source level path as
described in [runtime applications](runtime.md).

Enemy `maximum_health` belongs in `game/assets/config/animals.json` and defaults to
100 for existing definitions. Each spawn copies it into maximum/current health.
Enemy deaths grant no gold; the existing fixed wave rewards remain unchanged.

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

Debug and Release Game/Editor builds passed, with **29/29 CTest checks** passing
in each configuration. The Editor-disabled Release build passed its three focused
combat/Game checks. Vulkan validation logs contain no validation errors or
synchronization hazards. Build/test logs are in `out/phase22/`.
