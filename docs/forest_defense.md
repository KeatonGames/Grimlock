# Level 01 - Greenhaven Valley

Greenhaven replaces Forest Defense's disconnected platforms, ramps and bridge with
one connected, non-flat landscape. The level keeps its UUID, asset paths, five
waves, four construction locations, starting gold, tower-only roster and objective
combat rules. Existing menu discovery and progress records continue to work.

## Landscape

The visible heightfield covers 160 by 120 world units, with 321 by 241 vertices.
The playable area is 38 by 38 units, matching the existing characters, tower
ranges and wave travel times rather than enlarging gameplay distances. Terrain
extends far beyond this area, with rolling hills, rocky ridges and a deterministic
perimeter forest. There are no island sides, gaps, bridge or terrain grid.

The western forest entrance is at (10, 0, -3.5), beside the retained red portal,
crates, barrel and low fences. The elevated camera presents this entrance on the
left and the castle on the right. The southern meadow is an open, rolling combat
area. Two rocky ridges flank the road near (0, 0, 0), forming the central pass.
The road bends northward and gradually climbs to the castle approach at
(-8.5, 2, 10.8). Dirt color is blended into the terrain vertices and widens near
the castle, so there is no separate hovering road surface.

The hilltop defense objective retains the existing crypt/castle model, roof,
warm doorway, walls, banners and braziers. Its foundation meets a broad terrain
pad at Y=2. The eastern swale at approximately (-14, -7) is reserved for a future
stream. This version has no water simulation or water material.

| Construction area | Ground X/Y/Z | Role |
| --- | --- | --- |
| Entrance meadow | (5.5, 0, -4.6) | Covers the opening approach |
| Garden overlook | (7, 1, 4.6) | Covers the meadow and pass approach |
| Pass exit | (-4.5, 1, 5) | Overlooks the uphill turn |
| Castle hill | (-5.8, 2, 10.8) | Covers the final ascent |

All four areas have flat terrain pads and retain their saved tower
facings, construction IDs and one-tower policy. Gameplay tower placement checks
full footprint support on the actual generated triangles. Hills outside these
pads remain sloped. Enemy and hero movement uses the existing radius-specific
Recast/Detour surfaces, including slope limits, obstacles and tower avoidance.
The visible terrain outside the playable rectangle is excluded from navigation.

## Editing and regeneration

Open `assets/levels/forest_defense.json` in Game or Editor. The display name is
**Level 01 - Greenhaven Valley**. Game and Editor share the same saved scene.
See [terrain authoring](terrain.md) for the reusable heightfield component.

The terrain Inspector exposes dimensions, resolution, playable extent, seeded
noise, boundary height, colors, hills, flat pads and road points. Decorations,
construction points, portal and castle remain ordinary editable scene objects.
Use **Ground on terrain** after moving props or changing the surface elevation.
New model placement grounds itself on a heightfield beneath the camera target.
Changing terrain does not automatically relocate existing authored structures;
placement validation rejects unsupported buildings.

To regenerate the supplied layout from authored C++ parameters:

```powershell
.\tools\author_level_one.ps1
```

This builds and runs `GreenhavenAuthor`, which uses Engine height queries for
placement and Scene serialization for saving. It overwrites scene edits. The
level JSON retains its economy and wave data. No authoring tool runs at startup.

The camera keeps right-drag pan, middle-drag orbit and wheel zoom. Greenhaven
limits gameplay pan, pitch and zoom so the extended landscape fills the view.
The Editor retains free camera navigation for authoring.

## Waves and economy

The player starts with 20 gold and the objective has 10 health. Waves contain
6, 10, 12 and 12 Goblins, followed by one Goblin Boss and four Goblins. Individual
spawns remain 0.6 seconds apart. Every kill grants two coins; wave rewards remain
zero. Archer Towers cost 10 and Mage Towers cost 15; upgrades are unchanged.
Level 1 retains its empty `available_heroes` list. The terrain supports all hero
types; traversal checks deploy them independently without changing the roster.

## Validation

`Terrain.Heightfield` checks deterministic generation, seed variation, indices,
triangle winding, vertex normals, UVs, exact triangle height/normal interpolation,
ray intervals, boundaries, flat pads, invalid inputs and scene round trips.
`Level.Forest` checks the generated level, four construction areas, both tower
types and upgrades, portal-to-castle boss movement, all hero profiles, crowd
movement at multiple frame rates, gameplay waves, economy, Editor grounding and
save/reload. `Undead.Runtime` checks repeated objective attacks and collapse.

```powershell
cmake --build --preset debug --parallel 4
ctest --test-dir out/build/debug -C Debug --output-on-failure
cmake --build --preset debug --target MenuCapture --parallel 4
.\out\build\debug\Debug\MenuCapture.exe "$PWD\game\assets" --scene "$PWD\game\assets\scenes\forest_defense.json" "$PWD\game\assets\previews\forest_defense.png"
```

Implementation logs and renderer captures for this change use `out/greenhaven_*`.

Verified in Debug: Engine, Game and Editor build successfully. The complete suite
passed 52 of 53 checks initially; the exposed Inspector scroll regression was
fixed and `Editor.Runtime` passed on rerun, leaving all 53 checks passing.
Khronos core and synchronization validation were enabled for the final gameplay
capture and reported no validation errors or synchronization hazards. Four
maximum-distance, minimum-pitch camera-boundary captures show continuous scenery
without exposed terrain edges. Release was not rebuilt for this change.
The current minimalistic presentation uses flat-shaded terrain faces and a single green base color. Dirt-road coloring, rock tinting and grass color variation are disabled; the authored route still shapes navigable ground. The terrain uses 161 by 121 grid vertices for broader facets.

The playable floor is now entirely level at Y=0. A broad flat pad covers the playable area; angular mountains remain beyond its perimeter. The castle and all four construction locations are grounded at the same elevation.

Minimalistic perimeter slopes now use broad planar ramps around an irregular polygonal flat floor, eliminating the circular bank triangulation pattern.
The minimalistic banks are rendered as explicit polygon rings with coplanar slope faces, so their edges no longer show grid triangles.
