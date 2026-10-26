# Handcrafted levels and waves

Run `GrimlockEditor`, then choose **Navigation > Level > Load First Watch**.
Loading uses the editor's normal scene-opening and unsaved-change confirmation.
The **Level** panel shows objective health, wave name, pending and active counts,
and the current phase. Select a tower and use **Build Tower** in the Level panel
during preparation, then **Start Wave**. Towers must fit inside the level's authored
build zones; use **Navigation > Build Zones** to edit them. See [build zones](build_zones.md).
Each following wave requires another click.
**Restart Level** reloads the authored scene and resets towers, enemies, health and
wave progress. Opening another scene leaves level mode. Normal navigation debug
tools still work outside level mode; manual animal spawning and retargeting are
disabled during a level so they cannot alter the authored schedule.

Starting gold and fixed per-wave rewards belong to this level JSON; construction
costs belong to the tower catalog. The Level panel displays the runtime balance.
See [economy and construction](economy.md). The separate Navigation tower tools
remain free editor authoring controls.

First Watch is the first playable level: two meadow entrances converge through
the west gate, then split around the eastern wall into an expanded rear courtyard.
The three build zones support a west interception tower, central coverage on both
routes, and a protected rear battery. The gold marker at (7, 0) is the objective.
Start with 90 gold, one hero and 10 objective health; every arrival costs one health.
See [the level guide](first_watch.md) for the five exact waves, balance and tested plans.

## Authoring

`game/assets/levels/first_watch.json` contains gameplay configuration, separate
from `game/assets/scenes/first_watch.json`. The latter is a normal engine scene;
the current sample uses existing cube terrain. Level/wave data never addresses
grid cells: destinations, spawn rectangles and elevations use world coordinates.
Animal configuration remains in `assets/config/animals.json`.

For imported terrain and the grid-free Hill Watch test map, see [terrain authoring](terrain.md).

The level JSON has version `1`, a name, an asset-relative scene path, an objective
with world X/Z `position`, optional `height` (defaults to the first spawn's height), positive integer `health` and `damage_per_arrival`, named
spawn rectangles (`minimum`, `maximum`, `height`), and an ordered `waves` array.
Every named wave has explicit groups:

```json
{"enemy":"Chicken", "count":4, "spawn":"North meadow", "start":0, "batch_size":2, "interval":2}
```

`enemy` names an existing animal definition. `start` is seconds after Start Wave;
`batch_size` animals are requested together, with `interval` seconds between
batches. The last batch can be smaller to preserve the exact count. Groups with
the same start run concurrently; later starts create authored sequences. Zero
interval emits the explicitly listed count immediately. There is no randomness,
generated composition, scaling, or automatic next-wave start.

Counts, batch sizes, timing, coordinates, spawn references and scene paths are
validated. Each wave is limited to the existing 1,000-enemy capacity. Loading also
validates animal availability, despawning enabled, and spawn-to-objective routes
for every used profile. Occupied spawn slots retry in later updates without
discarding scheduled enemies or exceeding the authored count. Completely
obstructed spawn locations can therefore keep a wave pending until cleared.
Tower commits protect routes for all future groups, including profiles not yet
spawned. Cursor previews still perform only immediate footprint checks.

## Runtime responsibilities and verification

Game's `LevelDefinition` describes the level. `WaveSystem` owns the fixed schedule,
phase and objective health independently of rendering, navigation and ImGui.
`LevelSession` binds it to the existing NavigationSession/EnemySystem. Arrival
counts apply damage once, after movement removes arrivals. A wave completes only
after all scheduled enemies spawn and the active collection is empty, whether
enemies arrived or were removed. Zero health ends immediately in defeat; the final
completed wave with surviving health ends in victory. Preparation and terminal
phases do not run battle movement or spawning. Day/night currently names these
phases; it does not add lighting effects or a clock.

Editor exposes only generic scene-request, scene-open notification and panel hooks;
gameplay decisions remain in Game. Engine rendering, navigation algorithms and
scene JSON have no knowledge of waves or objective health. Runtime health and wave
progress are not written into scene files.

`Level.Playable` runs the actual authored level through five scripted player plans:
mixed towers, Small Tower coverage, rear defense, no construction and neglected
upgrades. It requires three distinct wins and two losses, exact scheduled totals,
one-time rewards, navigation containment, no stalled waves and clean restart.
`Levels.Waves` checks exact scheduling, simultaneous and sequential groups,
partial/occupied spawning, preparation transitions, health, victory, defeat,
restart and invalid definitions. `Enemies.Runtime` loads the level through ImGui,
places a preparation tower, runs a separate three-wave editor-control fixture with real navigation/rendered
animals, checks exact arrival damage and victory, reloads the scene, verifies
defeat, and leaves level mode safely.
