# Level 01 - Forest Defense

Choose **Level 01 - Forest Defense** in Game's existing level selection menu, or run:

```powershell
.\out\build\release\Release\Grimlock.exe assets/levels/forest_defense.json
.\out\build\release\Release\GrimlockEditor.exe --level assets/levels/forest_defense.json
```

For permanent authoring in the source asset tree, pass the absolute source level
instead of editing the build's staged copy:

```powershell
$forestLevel = (Resolve-Path game/assets/levels/forest_defense.json).Path
.\out\build\release\Release\GrimlockEditor.exe --level $forestLevel
```

The level uses the shared [level JSON](../game/assets/levels/forest_defense.json)
and [editable scene](../game/assets/scenes/forest_defense.json). Its stable UUID
associates results with the existing progression system. There is no level-specific
code in Game, no procedural generation, and no navigation grid.

## Layout and navigation

An irregular woodland meadow leads from the western camp at (-10, 0, -4) through
an S-shaped dirt trail and an open central clearing. A broad grassy ramp at
(4, 6) rises from Y=0 to a watch hill at Y=2. The defense objective is at
(4, 2, 12), in front of the forest obelisk. A separate ramp connects the clearing
to a rocky lookout at (-6.5, 1, 4.2).

Five continuous build zones cover the western ambush meadow, central clearing,
rocky lookout and both hill battlements. These are ordinary editable build-zone
objects, not snapped tower slots. For an elevated placement, try a Small or Large
Tower near (6.8, 2, 10.5). Upright towers still require flat support over their whole
footprint; ramps, cliff overhangs and path depressions remain invalid foundations.

The scene's terrain pieces supply their actual transformed triangles to the
existing Recast/Detour system. Agent radius, height, slope, step, crowd movement,
and dynamic tower-obstacle settings are unchanged. The broad hill ramp supports
the Iron Stag's one-metre collision radius. Solid boundary rocks and the obelisk
are obstacles; vegetation, flowers, signs and camp props are decoration.
Heroes can deploy freely on valid terrain using the current controls and limits.
Camera bindings are unchanged; only this scene's initial view was composed.

## Assets and editing

The 107 authored objects reference 22 distinct original Nature Kit GLBs. The main
pieces are `ground_grass`, `ground_pathStraight`, `ground_pathBend`,
`ground_pathEnd`, `cliff_block_rock`, `cliff_blockHalf_rock`, and
`cliff_blockSlope_rock`. Repeated `tree_default`, `tree_oak`, `tree_pineTallA`,
bushes, rocks and flowers frame the playable area. The camp, signs, obelisk,
columns and a small `platform_grass` detail identify the entrance and objective.
Original mesh geometry, palette, material settings and GLBs are retained, under
the supplied [CC0 license](../game/assets/environment/nature/kenney/LICENSE.txt).

The Editor discovers all 329 kit models in its existing **Add Model** picker.
Use **Filter models** with `kenney_nature/tree`, `cliff`, `ground` or `rock`.
Filtering to an empty result disables insertion rather than placing a hidden
previous selection. Translation, rotation, scale, selection, grouping, duplication,
role assignment and scene/level saving use the existing tools.

Ground modules are two metres across. The scene compensates the kit's -0.05
exported Y offset: ordinary ground uses Y=0.1 at scale 2; the hill's full cliff
blocks use the same transform and reach Y=2. Do not normalize individual terrain
pieces independently after placement, or adjacent edges will no longer align.

The supplied files have a nonconforming exporter wrapper: an unused identity
`tmpParent` still lists the declared scene root as a child. The importer now
detaches only unused, empty, nonanimated identity wrappers that are not roots in
any scene. Other parent/root conflicts retain strict validation. This is a narrow
compatibility repair; [glTF requires scene roots to be parentless](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#scenes).
No source GLB is rewritten. Existing handling of degenerate source triangles
continues to report them and omit zero-area triangles.

## Handcrafted waves and economy

The player starts with **120 gold** and an Assassin in the clearing. Ranger and
Warrior deployment remains available through the current hero selector. The
objective has **10 health**, with one point lost per arriving enemy.

| Wave | Exact composition | Fixed reward |
| --- | --- | --- |
| Scouts on the trail | 6 Chickens | 30 |
| Beneath the roots | 6 Chickens, 4 Black Rats | 40 |
| The woodland raiders | 6 Grunts, 4 Dogs, 2 Siege | 50 |
| Hold the ascent | 6 Grunts, 4 Hunters, 2 Siege | 70 |
| The Iron Stag | 8 Grunts, 2 Siege, 1 Iron Stag | 0 |

All 51 enemies, batch sizes, intervals and group start times are explicit in the
level JSON. The final boss starts 19 seconds into wave five. Tower/enemy/hero
definitions and combat statistics are unchanged. Start each wave with the
existing **Start Wave** button; preparation, rewards, victory, defeat, retry and
level selection retain their normal lifecycle.

One verified defense uses a Small Tower at (-8, 0, -2), Large Tower at (0, 0, 0),
and Small Tower at (6.8, 2, 10.5). Reposition the Assassin near (-6, 0, -2), deploy
Ranger near (2, 0, 0) and Warrior near (-4, 0, 0), and improve surviving towers
between waves. Cleave helps against clustered attackers. This plan completes all
five waves and kills the boss with the objective intact; leaving the initial hero
without tower construction loses. These checks establish playable win/lose paths,
not that every placement combination is equally effective.

## Checks and limits

`Level.Forest` imports all 329 GLBs, checks discovery, moves all hero types up the
hill, walks the full route with the boss footprint, validates elevated Game tower
placement/cost, downhill projectile hits, and resource sharing, round-trips scene
data, and exercises Editor discovery/duplication/transform/save commands when
Editor is enabled. It plays the
defended and undefended strategies, verifies exact rewards, boss death/health-bar
activation, isolated persistent results, restart and standalone-instance cleanup.
The two-triangle foundation case also has a focused regression in `Terrain.Runtime`.

```powershell
ctest --test-dir out/build/release -C Release -R '^Level.Forest$' --output-on-failure
```

Verified on Windows on 2026-11-05: Game and Editor build with both CMake presets.
All **43/43 Release checks** passed (165.84 seconds), and all **43/43 Debug checks**
passed (374.80 seconds). Both suites ran with `VK_LAYER_KHRONOS_validation` and
synchronization validation enabled; their logs contained no validation errors.
Modified C++ files pass `clang-format --dry-run --Werror --style=file`, and
`git diff --check` is clean. Game and Editor were also launched and visually
reviewed with this level.

Original character assets retain their existing animation availability; this phase
does not add clips. Broad navigation rebuilds remain synchronous. Decorative
vegetation intentionally has no collision. Automated playthroughs and short repeated
loads do not establish long-session leak freedom or exhaustive gameplay balance.
Progression checks use isolated saves in the build output. The direct Game capture
showed **Save unavailable** for the normal profile; saving to the normal user-data
directory was not verified. No user save-file repairs or resets were performed.
Game and Editor captures and verification logs are under ignored `out/phase41`.
