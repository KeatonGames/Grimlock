# Enemy archetypes

Enemy definitions and behavior remain in GameLogic. All types share EnemyCombatSystem,
EnemyAi, Recast/Detour movement, timed animation impacts and the existing damage API.
There is no model-name branching, separate pathfinder or new behavior framework.

## Definitions

`game/assets/config/enemy_archetypes.json` adds named types to the existing animal
catalog. Each entry references an `animal` from `config/animals.json` for its model,
animations, visual scale, physical height/radius and movement handling. Change that
reference to use another existing animal without changing the targeting policy.
An optional `radius` overrides the inherited physical footprint independently of
visual scale. Definitions are loaded once; existing instances retain their type.
Model/GPU resources are cached by Engine and shared across types and instances.

| Type | Animal | Health | Speed | Damage | Interval | Range | Detection / pursuit | Preference |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| Grunt | Chicken | 120 | 1.4 | 8 | 1.0 | 0.35 | 2.6 / 4 | Objective; local obstruction or engagement |
| Siege | Deer | 300 | 0.95 | 14 | 1.2 | 0.40 | 4.5 / 6 | Towers, heroes, objective |
| Hunter | Dog | 80 | 2.2 | 9 | 0.7 | 0.35 | 4 / 5.5 | Heroes, towers, objective |

`maximum_health` and `speed` are explicit per archetype. The `combat` object supports
`damage`, `range`, `attack_interval`, `impact_time`, `aggression_range`, `pursuit_range`,
`hero_priority`, `tower_priority`, `objective_priority` and `attack_animation`.
Larger priorities win, with nearest reachable targets breaking ties. Zero disables
opportunistic acquisition of that category; objective arrival still works.

`hero_damage_modifier` and `tower_damage_modifier` default to 1. Siege explicitly
uses 2 against towers (28 damage), and Hunter uses 1.5 against heroes (13.5 damage).
These affect the shared impact damage calculation, not health, navigation or model
scale. Objective arrivals keep the level's existing `damage_per_arrival` rule.
All numeric inputs are validated, including finite, nonnegative modifiers and
finite resulting damage.

`objective_first: true` restricts defender acquisition to nearby attackers or a
defender ahead within `obstruction_range` of the enemy's physical surface. Grunts
recognize a hero actively targeting them and remember the actual source of damage
for `threat_memory` seconds (2 by default). They resume their objective when that
local threat disappears. These options are also accepted in legacy animal combat
definitions; omitted fields preserve existing behavior.

## Target lifetime and navigation

Targets use stable IDs. Valid targets are retained rather than switching every
frame. Acquisition runs every 0.4 seconds, checks priorities and reachable melee
positions, and tries at most four candidates per pass. Failed batches rotate so
unreachable preferred defenders do not starve a reachable fallback. Rejected
active targets have a two-second retry delay. Pursuit is bounded from the initial
engagement position; a target that escapes that radius must return before the same
exhausted pursuit can start again. Enemies otherwise continue toward their objective.

Approach positions and impacts retain the existing per-size NavMesh, elevation,
physical contact and combat visibility checks. Damage still occurs once at the
configured impact time. Dead targets and removed towers are revalidated before
damage, and existing crowd steering, health feedback and death cleanup are reused.

## Authoring and test level

In Editor, choose **Navigation > Level > Load Rival Watch**. The existing level
definition editor's **Enemy Type** dropdown includes Grunt, Siege, Hunter and legacy
animals. Counts, spawn areas, batches, start times and intervals remain authored
explicitly. No level JSON schema change is required: wave groups reference a named
definition through their existing `enemy` field. Old catalogs without an archetype
file and existing animal-based levels continue to load.

Run the same scenario in Game:

```powershell
.\out\build\release\Release\Grimlock.exe assets/levels/rival_watch.json
```

Rival Watch reuses Cover Watch's mountain, ramp, elevated build zones, tower and hero.
Its four waves are Grunts; Grunts/Hunters; Grunts/Siege; all three. It has 150 starting
gold and fixed rewards of 35, 45, 55 and 0. Its deliberately forgiving 50-health
objective allows all four diagnostic waves to run; First Watch remains the default
playable level and its balancing is unchanged. Start each wave from the normal HUD.

The existing `Enemies.Definitions` and `Combat.Battle` checks cover archetype data,
legacy compatibility, invalid input, target preferences, interception/disengagement,
category damage, pursuit limits and the complete four-wave elevated scenario.

Debug and Release builds include both Game and Editor. The complete suites passed
34/34 tests in each configuration (229.60 seconds Debug, 105.33 seconds Release),
with Vulkan validation enabled. Logs are in `out/phase29/`. Checks include existing
First Watch strategies, tower upgrades/economy, hero controls and abilities,
terrain/visibility, crowd movement, assets, rendering and Editor interactions.
Modified C++ files pass the root clang-format check and Git whitespace validation.
