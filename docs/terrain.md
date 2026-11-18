# Continuous and imported terrain

## Heightfield scene component

Engine's `Heightfield` generates an immutable connected surface from
`HeightfieldConfiguration`. The scene object `heightfield` field stores authored
parameters, not baked triangles. Scene load regenerates the same model used by
the Vulkan renderer and triangle picking. No additional renderer or asset format
is required. A heightfield and imported `model` are mutually exclusive on an object.

Use **Create Terrain** in the Editor hierarchy. Select the object and expand
**Terrain generation** to configure visual dimensions, vertex resolution (2 to
513 vertices per axis), playable dimensions, base height, seeded smooth noise,
boundary hills, grass/dirt/rock colors, hills, flat pads and road points.
Colors are linear RGB. Roads are authored XYZ polylines with width and end width;
terrain elevation and vertex color blend around the route. Pads flatten a disc
and smoothly join the surrounding terrain. Hills use radial smooth profiles.
The last pads in the list take precedence where they overlap.

`Heightfield::sample(localXZ)` returns the exact rendered triangle elevation and
face normal, including both halves of a cell and the outer edge. `intersect(ray)`
returns a hit on the same triangles, respecting the supplied interval. Queries
outside visual bounds return no sample. Scene transforms apply to the generated
model in rendering, picking, and navigation; direct Heightfield queries are local.

Mark the component `grimlock.terrain`. The game feeds its generated triangles
within `playable_size` into the existing Recast pipeline. Scenery beyond that
extent still renders. The maximum slope and agent clearance rules remain the
same as for imported terrain. Existing obstacles, full-footprint building support,
combat cover, enemy movement and hero movement reuse those triangles. Terrain
parameter changes invalidate the navigation source; ordinary frames reuse it.

**Ground on terrain** positions an edited object's lowest transformed bound on
the heightfield, including objects with parents. Add Model uses this automatically
when a heightfield is beneath the camera target. Terrain edits do not silently
move existing decorations or construction areas; re-ground them and check
navigation before saving. Flat pads should cover complete building footprints.
Game still rejects unsupported or overlapping tower placement.

This implementation uses one mesh, smooth vertex normals, UVs and existing PBR
vertex-color materials. It has no streaming, LOD, sculpting brushes or water
simulation. `Terrain.Heightfield` provides CPU generation/query/serialization
checks; `Level.Forest` exercises the generated surface through gameplay and Editor.

## Imported terrain fixtures
Run the handcrafted test level from the repository root after building:

```powershell
out/build/debug/Debug/Grimlock.exe assets/test_fixtures/levels/hill_watch.json
```

In Editor, choose **Navigation > Level > Load Hill Watch**. The scene has no
navigation grid: its GLB contains a lower meadow at Y=0, a ramp from X=2 to X=6,
a hill platform at Y=2, and a deliberately disconnected overlook at Y=3.
The lower entrance sends animals uphill toward the objective at (10, 2, 0).
Build zones exist on the meadow, hill, and overlook. Try a Large Tower near
(7.7, 2, 1.2), or command the hero from the meadow onto the hill during battle.
The overlook cannot be reached without an authored connection.

## Importing a map

1. Export a static, triangulated GLB from Blender with Y up and metre-sized units.
   Terrain navigation uses static rest geometry; skinned/morphed terrain is rejected.
2. Add the model through the existing model browser. Select it in the hierarchy
   and choose **Level geometry > Terrain / navigation surface** in the Inspector.
   Set the scene transform as intended; the model browser initially normalizes
   added models, so use Scale (1, 1, 1) for an export already authored in world units.
3. Add as many terrain sections as needed. Imported node transforms and scene
   hierarchy transforms are included in navigation; rendering retains original
   materials and meshes. Mirrored transforms preserve surface winding.
4. Mark blocking props **Navigation obstacle**, and visual-only props **Decoration**.
   Explicit obstacles use their triangles and conservative solid bounds, preventing
   navigable interiors. Legacy props retain their existing placement/hero blocking.
5. Use **Navigation > Show NavMesh** to inspect walkable areas. Terrain role,
   enabled state, model and world transform changes rebuild affected navigation.
   Ordinary hero movement does not rebuild it. Tower placement/removal still
   rebuilds obstacle geometry and invalidates borrowed path revisions safely.

Terrain triangles replace the grid as navigation input when any enabled terrain
model exists. Old grid scenes still work as debugging fixtures; real maps need no
`navigation_grid`. A grid left in a terrain scene remains a visual debugging aid.

Recast retains per-profile radius and height clearance, a 45-degree maximum slope,
and a 0.15 m maximum step (`NavMeshConfiguration`). Detour constrains movement to
the connected surface and supplies its Y coordinate. Surface visibility also
checks the reached elevation, preventing X/Z raycasts from jumping between floors.
Crowd contacts ignore bodies whose vertical intervals do not overlap.

## Elevated construction and goals

Build zones keep their existing scene role and transform serialization. In
**Navigation > Build Zones**, edit all three position coordinates, yaw, width and
depth. A zone's Y identifies its intended foundation elevation (5 cm tolerance),
so a zone above another floor does not authorize construction on the floor below.
Create separate zones for different elevations; there are no slots or grid snaps.

Cursor placement intersects cached world-space terrain triangles, not a ground
plane or a mesh's bounding box. Towers stay upright. Validation clips terrain
triangles against the full rotated footprint, checks support and obstructing
surfaces, then checks zones, existing towers, props and living units. Foundations
allow at most 10 degrees of slope and 4 cm of surface variation around the base;
steeper ramps, cliffs, holes and floating/intersecting placements are rejected.
This small tolerance accommodates gently uneven foundations without tilting models.
Route rebuilds remain commit-time work; cursor previews do not bake a NavMesh.

Level objectives now accept an optional `height` alongside their existing X/Z
position. Omitted height uses the first spawn's height, preserving existing flat levels:

```json
"objective": {"position": [10, 0], "height": 2, "health": 10, "damage_per_arrival": 1}
```

Spawn rectangles already have `height`, and hero spawns remain full XYZ positions.
The Editor exposes objective, debug destination and spawn heights. These height
hints select the intended floor during projection, including layered maps.

Tower acquisition uses full 3D distances between terrain-level entity anchors;
visual tower height does not alter its configured range. Existing projectiles
already travel in 3D. Melee uses a 3D gap plus surface visibility and contact-height
checks, allowing slope combat while rejecting inaccessible vertical gaps.

## Checks

`Terrain.Runtime` imports and renders the GLB, tests transformed/cached geometry,
ramps, stairs, slope/height limits, stacked floors, crowd movement at 10/30/120 FPS,
hero ascent, elevated placement/removal, cross-height projectiles, scene/level
round trips, wave completion and restart. The remaining CTest suite covers existing
flat gameplay, rendering, assets, input, economy, combat and Editor interactions.

Historical imported-terrain verification: Game and Editor build in Debug and Release, with no compiler warnings;
all 33 CTest checks pass in both configurations (Release 99.26 s, Debug 218.15 s).
That verification reported no Vulkan errors or synchronization hazards. Formatting and
whitespace checks pass. Logs are under `out/archive/terrain/`.

For the subsequent solid-cover, projectile collision and visibility work, see
[combat and cover](combat.md). That historical verification passed all 34 tests in both
configurations; logs are under `out/archive/phase28/`.

Current Greenhaven heightfield build, test and validation results are recorded in
[the level guide](forest_defense.md#validation).
