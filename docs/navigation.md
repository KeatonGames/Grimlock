# NavMesh navigation

Phase 14 uses the official [Recast Navigation 1.6.0 release](https://github.com/recastnavigation/recastnavigation/releases/tag/v1.6.0). CMake FetchContent pins its archive and SHA-256 digest. Only Recast and Detour are compiled; DetourCrowd, tile cache, upstream demos and tests are not included. Upstream sources are unchanged. The small CMake wrapper avoids the release's obsolete CMake minimum, following the project's existing dependency approach.

## Running the example

```powershell
cmake --preset debug -DGRIMLOCK_BUILD_EDITOR=ON -DGRIMLOCK_BUILD_TESTS=ON
cmake --build --preset debug --parallel 4
.\out\build\debug\Debug\Grimlock.exe --editor assets/scenes/chickens.json
ctest --test-dir out/build/debug -C Debug --output-on-failure
```

In **Navigation**, click **Spawn Chickens** (default 100), set **Destination X/Z**, or adjust **Movement Speed** and **Spawn Area (world X/Z)**. **Clear Chickens** releases runtime instances. Standalone Game mode automatically spawns 100 chickens on terrain scenes. Spawn positions and destinations are projected onto the NavMesh without snapping to grid centers. Spawns that have no navigable surface or insufficient separation are skipped. Scenes without navigation geometry continue rendering/editing normally but cannot spawn navigating chickens.

**Show NavMesh** displays translucent cyan polygons using the existing ImGui overlay. It is off by default, independent of wall editing, and captures no viewport input. Like the grid overlay, it is a debug projection without depth occlusion. **Rebuild NavMesh** explicitly replaces navigation data. Status, polygon count, build time and complete-path query count are shown next to the existing enemy CPU/frame statistics. Grid lines and cell highlights appear only during **Edit Walkability**. Cube floors and walls remain visible normally. Runtime enemies and navigation settings are not serialized; the existing scene/grid JSON format is unchanged.

## Geometry and ownership

Engine's `terrainBoxes()` describes the actual world-space cubes: floor centers/extents, wall centers/extents and whether a top surface is walkable. The renderer and Game geometry adapter consume these same descriptions, avoiding separate terrain dimensions or guessed obstacle positions.

`NavigationGeometry` accepts indexed world-space triangles with walkable-surface flags. Its box helper adds outward-facing triangles and solid blocking volumes. Floor tops can be walkable; wall tops are always blocking. Solid volumes remove any buried floor spans inside cube shells. Decorative scene models are deliberately excluded.

`NavMesh` has no Grid, Enemy, Vulkan or ImGui dependency. Recast rasterizes the triangles, filters insufficient headroom and ledges, excludes solid volumes, erodes for agent radius, partitions regions, and produces polygons/detail height data. Detour owns the resulting navigation mesh. All temporary Recast objects, mesh bytes, meshes and query workspaces use RAII with the corresponding native free functions. A single shared mesh and query workspace serve all enemies on the Game thread.

For future GLB levels, an importer or level adapter can transform selected triangles into world space and populate the same geometry structure. Mark walkable versus blocking surfaces explicitly and supply solid volumes where a shell encloses otherwise walkable geometry. The build/query algorithms need no changes. Automatic GLB collision extraction and collision-mesh authoring are outside this phase.

## Paths and movement

Detour projects endpoints onto nearby polygons, searches a polygon corridor, and applies funnel/string pulling to find continuous world-space corners. Open routes steer straight to the projected goal. Complete queries reject partial, truncated or exhausted searches: an unreachable destination stops the enemy instead of reporting arrival at the closest reachable polygon. Endpoint projection searches within one world unit horizontally; distant or vertically incompatible destinations fail safely.

Each enemy owns a cached `NavigationPath`, requested destination, retry delay and stuck timer. The path stores polygon references and the NavMesh revision, never native pointers. Destination changes and rebuilds request a new complete path. Failed routes wait for those changes. Leaving a corridor or remaining stuck requests a bounded retry, at most once per second; normal movement does not search for a complete path every frame. Funnel queries use stack scratch buffers, trim passed polygons and anticipate nearby corners. Movement remains constrained to the surface when anticipating turns, so it cannot cut through walls.

The existing spatial hash, circular footprints, local separation, contact sliding, overlap recovery, acceleration/deceleration and smoothed facing stay in Game systems. Separation priority uses remaining funnel-route distance. All proposed movement and overlap corrections pass through Detour `moveAlongSurface`, including boundary clamping and surface-height sampling. Rounded boundary results are clamped to the same visited polygon when height queries need a precision fallback. Separate agents can take different local trajectories where space allows; narrow passages naturally become single-file queues.

Movement uses bounded substeps (at most 0.25 seconds and 64 substeps after a stall). Enemies stop within their physical radius of the projected destination. The existing shared definition's `despawnOnArrival` option controls removal and remains enabled by default. Physical radius remains independent of `visualHeight`. Chicken assets share mesh/material/texture resources; the supplied chicken has no clips and retains rest pose. Animated configurations still pause/resume independent playback according to movement.

## Configuration

`NavMeshConfiguration` uses world units and degrees:

| Parameter | Default | Purpose |
| --- | --- | --- |
| `cellSize` | 0.10 | Recast voxel resolution in X/Z; independent of debug terrain cells |
| `cellHeight` | 0.05 | Vertical voxel resolution and resulting height precision |
| `agentRadius` | 0.22 | Minimum boundary clearance; rounded up to whole voxels |
| `agentHeight` | 0.50 | Required headroom |
| `maximumSlope` | 45 | Maximum walkable surface angle in degrees |
| `stepHeight` | 0.15 | Maximum traversable vertical step; rounded down to voxels |

The scene session takes radius from the shared chicken definition. A mesh may serve different physical radii up to its baked radius, conservatively giving smaller agents the same boundary clearance. Build for the largest supported type; spawning larger agents is rejected rather than silently allowing unsafe clearance. Voxel erosion, conservative rasterization and ledge filtering may close passages near the minimum physical width. Reduce voxel size when appropriate, within the build budget. These parameters are intentionally configured in code rather than through more editor controls.

The prototype uses one mesh, a 16-million-column raster budget, at most 32,767 columns per axis and the Recast vertical span limits. The baked radius may occupy at most 127 voxels to avoid overflowing Recast's byte-sized erosion distances. Queries use 4,096 Detour search nodes and at most 512 corridor polygons/corners. Local movement has a 64-polygon visited buffer and relies on bounded substeps. Invalid inputs, unsupported dimensions, failed builds and exhausted queries stop safely and report status. There are no background builds, streaming, off-mesh links, physics, combat or automatic decorative-model obstacles.

## Geometry changes

The scene session compares the terrain geometry source's revision. An unchanged scene reuses its mesh every frame. Wall changes rebuild the small mesh on the next Game update; multiple changes before that update share one rebuild. The revision is only a dirty signal: navigation queries and steering never read grid cells or walkability values. A rebuild or reset, including a failed build, increments the NavMesh revision and discards native data. Enemies stop or obtain fresh corridors before using polygon references again. Scene replacement and terrain resizing retain the existing preview cleanup behavior.

## Verification

- `Navigation.NavMesh`: geometry independent of grid cells, continuous projection, direct/funnel routes, blocked wall tops and buried floors, exact boundary handling, radius/headroom/slope/step settings, disconnected regions, invalid inputs, failed builds and revision invalidation.
- `Enemies.Crowd`: 15/30/60/144 FPS consistency, arbitrary-angle direct movement, braking/arrival, cached and unreachable paths, rebuilds, mixed radii, overlap recovery, frame spikes, 100 agents through a corridor, 500 on open terrain, and all 100 through the authored chicken map's narrow openings.
- `Enemies.Runtime`: actual ImGui spawn/clear, default-hidden and toggleable NavMesh overlays, explicit/automatic rebuilds, 100/500 rendered chickens, wall edits, shared resource lifetimes, independent physical radius/visual scale, animated enemy playback and missing geometry.
- Existing Engine, camera, picking, terrain serialization, model, renderer and Editor checks remain enabled.

Complete Debug and Release builds succeeded. The full CTest suites passed **19/19** in each configuration (Debug 123.17 seconds, Release 57.68 seconds). Runtime checks used `VK_LAYER_KHRONOS_validation` with synchronization validation enabled; logs contained no validation errors or synchronization hazards. Final radius-overflow and solid-bound checks were additionally rerun through `Navigation.NavMesh` in both configurations after rebuilding. All 23 added/modified C++ files passed `clang-format --dry-run --Werror --style=file`; `git diff --check` passed.

Release measurements on the development machine, over 120 rendered frames with validation enabled:

| Rendered chickens | Average enemy CPU update | Median frame | 95th percentile frame |
| --- | --- | --- | --- |
| 100 | 0.30 ms | 4.15 ms | 4.58 ms |
| 500 | 1.80 ms | 6.37 ms | 7.40 ms |

All 100 agents completed the authored narrow-corridor chicken map in 96.67 simulated seconds, with 402 complete-path queries across the run. Open-floor tests needed exactly one complete query per agent: 100 agents arrived in 31.68 simulated seconds and 500 in 80.28. These are local prototype measurements, including the existing model/animation/editor workload, rather than performance guarantees. NavMesh build times are available in the Navigation menu. Test and build logs are under `out/phase14/`.

## Changed files

| Area | Files |
| --- | --- |
| Added dependency wrapper | `cmake/Recast.cmake`; integrated from `cmake/Dependencies.cmake` |
| Added shared terrain geometry | `engine/include/grimlock/terrain.hpp`, `engine/src/scene/terrain.cpp` |
| Updated terrain rendering/build | `engine/src/renderer/grid_instances.cpp`, `engine/CMakeLists.txt` |
| Added navigation | `game/src/navigation/navigation_geometry.{hpp,cpp}`, `game/src/navigation/nav_mesh.{hpp,cpp}` |
| Updated lifecycle and integration | `game/src/navigation/navigation_session.{hpp,cpp}`, `game/src/entities/enemy.hpp`, `game/src/systems/{enemy_system,crowd_movement}.{hpp,cpp}`, `game/CMakeLists.txt` |
| Updated debugging | `game/src/debug/navigation_debug.{hpp,cpp}`, `editor/include/grimlock/grid_debug.hpp`, `editor/src/{editor.cpp,grid_tools.hpp,grid_tools.cpp}` |
| Added/updated checks | `tests/nav_mesh.cpp`, `tests/enemy_crowd.cpp`, `tests/enemy_runtime.cpp`, `tests/CMakeLists.txt` |
| Removed obsolete grid collision | `game/src/navigation/grid_clearance.{hpp,cpp}`, `tests/grid_clearance.cpp` |
| Documentation | `README.md`, `docs/navigation.md` |
