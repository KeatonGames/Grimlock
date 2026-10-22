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

First Watch has two meadow spawn areas, a winding approach, and a gold objective
marker. Its three waves contain exactly 4 chickens, then 4 chickens with 4 rats,
then 2 bunnies followed by 4 chickens. The objective starts with 30 health and loses
one health per arrival. This initial test is deliberately survivable without
combat: all 18 arrivals leave 12 health. Towers redirect navigation; they do not
attack. A level with fewer health points demonstrates defeat using the same rules.

## Authoring

`game/assets/levels/first_watch.json` contains gameplay configuration, separate
from `game/assets/scenes/first_watch.json`. The latter is a normal engine scene;
the current sample uses existing cube terrain. Level/wave data never addresses
grid cells: destinations, spawn rectangles and elevations use world coordinates.
Animal configuration remains in `assets/config/animals.json`.

The level JSON has version `1`, a name, an asset-relative scene path, an objective
with world X/Z `position`, positive integer `health` and `damage_per_arrival`, named
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

`Levels.Waves` checks exact scheduling, simultaneous and sequential groups,
partial/occupied spawning, preparation transitions, health, victory, defeat,
restart and invalid definitions. `Enemies.Runtime` loads the level through ImGui,
places a preparation tower, runs all three waves with real navigation/rendered
animals, checks exact arrival damage and victory, reloads the scene, verifies
defeat, and leaves level mode safely.
