# NavMesh navigation

Phase 14 uses the official [Recast Navigation 1.6.0 release](https://github.com/recastnavigation/recastnavigation/releases/tag/v1.6.0). CMake FetchContent pins its archive and SHA-256 digest. Only Recast and Detour are compiled; DetourCrowd, tile cache, upstream demos and tests are not included. Upstream sources are unchanged. The small CMake wrapper avoids the release's obsolete CMake minimum, following the project's existing dependency approach.

## Running the example

```powershell
cmake --preset debug -DGRIMLOCK_BUILD_EDITOR=ON -DGRIMLOCK_BUILD_TESTS=ON
cmake --build --preset debug --parallel 4
.\out\build\debug\Debug\Grimlock.exe --editor assets/scenes/chickens.json
ctest --test-dir out/build/debug -C Debug --output-on-failure
```

In **Navigation**, choose **Animal Model**, click **Spawn Animals** (default count 100), set **Destination X/Z**, or adjust **Movement Speed** and **Spawn Area (world X/Z)**. Chicken is selected initially. Model and speed changes apply only to future spawns; existing animals keep their definition, radius, scale, speed, animation and navigation profile. **Clear Animals** releases all runtime animals. Standalone Game mode automatically spawns 100 chickens on terrain scenes. Spawn positions and destinations are projected onto the NavMesh without snapping to grid centers. Spawns that have no navigable surface or insufficient separation are skipped. Scenes without navigation geometry continue rendering/editing normally but cannot spawn navigating chickens.

**Show NavMesh** displays translucent cyan polygons using the existing ImGui overlay. It is off by default, independent of wall editing, and captures no viewport input. Like the grid overlay, it is a debug projection without depth occlusion. **Rebuild NavMesh** explicitly replaces navigation data. Status, polygon count, build time and complete-path query count are shown next to the existing enemy CPU/frame statistics. Grid lines and cell highlights appear only during **Edit Walkability**. Cube floors and walls remain visible normally. Runtime enemies and navigation settings are not serialized; the existing scene/grid JSON format is unchanged.

## Geometry and ownership

Engine's `terrainBoxes()` describes the actual world-space cubes: floor centers/extents, wall centers/extents and whether a top surface is walkable. The renderer and Game geometry adapter consume these same descriptions, avoiding separate terrain dimensions or guessed obstacle positions.

`NavigationGeometry` accepts indexed world-space triangles with walkable-surface flags. Its box helper adds outward-facing triangles and solid blocking volumes. Floor tops can be walkable; wall tops are always blocking. Solid volumes remove any buried floor spans inside cube shells. Decorative scene models are deliberately excluded.

`NavMesh` has no Grid, Enemy, Vulkan or ImGui dependency. Recast rasterizes the triangles, filters insufficient headroom and ledges, excludes solid volumes, erodes for agent radius, partitions regions, and produces polygons/detail height data. Detour owns the resulting navigation mesh. All temporary Recast objects, mesh bytes, meshes and query workspaces use RAII with the corresponding native free functions. One shared mesh/query workspace per requested physical radius/headroom pair serves all matching animals on the Game thread. The geometry input is reused between profiles. Profiles are built lazily on selection and are kept at stable addresses until after the enemies are cleared; matching types reuse the same profile.

For future GLB levels, an importer or level adapter can transform selected triangles into world space and populate the same geometry structure. Mark walkable versus blocking surfaces explicitly and supply solid volumes where a shell encloses otherwise walkable geometry. The build/query algorithms need no changes. Automatic GLB collision extraction and collision-mesh authoring are outside this phase.

## Paths and movement

Detour projects endpoints onto nearby polygons, searches a polygon corridor, and applies funnel/string pulling to find continuous world-space corners. Open routes steer straight to the projected goal. Complete queries reject partial, truncated or exhausted searches: an unreachable destination stops the enemy instead of reporting arrival at the closest reachable polygon. Endpoint projection searches within one world unit horizontally; distant or vertically incompatible destinations fail safely.

Each enemy owns an independent destination, cached `NavigationPath`, retry delay and stuck timer. The path stores polygon references and the NavMesh revision, never native pointers. Destination changes and rebuilds request a new complete path immediately. Failed routes, displaced corridors and stuck enemies retry on a configurable cooldown (one second by default); normal movement does not search for a complete path every frame. Funnel queries use stack scratch buffers, trim passed polygons and anticipate nearby corners. Movement remains constrained to the surface when anticipating turns, so it cannot cut through walls.

The existing spatial hash, circular footprints, local separation, contact sliding, overlap recovery, acceleration/deceleration and smoothed facing stay in Game systems. Separation priority uses remaining funnel-route distance. All proposed movement and overlap corrections pass through Detour `moveAlongSurface`, including boundary clamping and surface-height sampling. Rounded boundary results are clamped to the same visited polygon when height queries need a precision fallback. Separate agents can take different local trajectories where space allows; narrow passages naturally become single-file queues.

Movement uses bounded substeps (at most 0.25 seconds and 64 substeps after a stall). Enemies stop within their configured stopping distance of the projected destination, measured along the route; zero uses their physical radius. Animals despawn on arrival by default, releasing their render instances and physical footprints to keep the destination clear. Set `despawn_on_arrival` to false to retain reached animals for inspection or retargeting; retained animals still occupy space. Physical radius remains independent of `visualHeight`. Chicken assets share mesh/material/texture resources; the supplied chicken has no clips and retains rest pose. Animated configurations still pause/resume independent playback according to actual movement.

## Enemy AI (Phase 15)

`systems/enemy_ai.{hpp,cpp}` implements a small FSM using the existing Detour queries. State, goal and timers live in `entities/enemy.hpp`; per-type settings remain in `EnemyDefinition`. There are no state objects, inheritance or new navigation algorithms. `CrowdMovement` schedules AI preparation, physical movement and movement observation inside each bounded substep. Decisions stay separate from circle avoidance, transforms and animation ownership in `EnemySystem`.

| State | Behavior and transitions |
| --- | --- |
| Idle | No assigned destination; velocity is zero and movement animation pauses. Assigning a goal starts Moving. |
| Moving | Follows its cached corridor, retaining existing acceleration, compact separation and smooth facing. Arrival becomes Reached; invalid navigation or insufficient progress becomes Blocked. |
| Reached | Despawns inside the stopping distance by default. With despawning disabled, stops and pauses movement animation until retargeted or the navigation revision changes. |
| Blocked | Stops safely, including when a wall is painted underneath it. Retries after the cooldown; destination/revision changes bypass the delay. Successful queries return to Moving or Reached. |

Stuck detection measures actual horizontal movement against 2% of the configured speed over `stuck_timeout` seconds (default two). An intentionally zero-speed animal does not accumulate stuck time. Retry and stuck timers use simulated substep time, including the existing bounded treatment of frame stalls.

The existing **Navigation** menu displays counts for all four states. **Inspect Enemy** selects a stable enemy ID and shows its model, state, position and independent **Enemy Destination X/Z**. Editing that value retargets only that enemy; **Clear Enemy Destination** makes it Idle. **Destination X/Z** assigns the shared testing goal to all current animals and future spawns. Ordinary frame updates preserve individual overrides. Changing the shared goal explicitly replaces those overrides. Scene replacement/reset clears runtime animals as before; AI state is not serialized.

`EnemySystem::setDestination(id, goal)` provides the same per-enemy operation without ImGui; `find(id)` supports inspection without relying on packed storage indices. Returned pointers are only valid until the next storage mutation. `setSharedDestination(goal)` broadcasts deliberately. New enemies inherit the shared goal and start Moving automatically when it exists. These APIs and AI logic have no species-specific branches.

## Configuration

Animal settings are separate from runtime data in `entities/enemy_definition.{hpp,cpp}` and `game/assets/config/animals.json`. The catalog lists installed animal GLBs; unavailable entries are omitted, duplicate/invalid definitions are rejected, and chicken remains first/default. Non-animal assets such as the material comparison, hierarchy fixture and assassin are not offered. Adding an animal requires only a GLB and a catalog entry, without species-specific branches in EnemySystem.

Each definition sets `radius`, physical `height`, independent `visual_height`, `speed`, optional `acceleration`/`deceleration`, `movement_animation`, `animation_speed`, `forward_yaw` (radians), and `despawn_on_arrival` (default true). Optional AI settings are `stopping_distance` (default zero, using radius), `retry_interval` (default one second) and `stuck_timeout` (default two seconds). Distances must be finite/nonnegative and timer durations finite/positive. Settings are copied into each animal at spawn. Physical circles were calibrated to the framed model's XZ dimensions at its configured visual height; runtime scaling never recalculates radius. If changing visual height in the catalog, review the radius independently. Tests verify footprint coverage. The supplied models vary greatly in source units, so `visual_height` normalizes their rendered height explicitly.

One EnemySystem holds all runtime animals with stable type indices, a shared spatial neighbor lookup, and per-type model/animation caches. Matching instances reuse Engine's cached model/material/texture resources. Animation selection uses the configured exact clip when present, otherwise prefers a walking loop and then a running loop, skipping start/end transitions. Assets without movement clips remain at rest pose. The Black Rat provides walking animations; most supplied animal assets are static, and the bunny assets retain their existing warned bind-pose preview behavior.

Chickens have radius 0.195 and visual height 0.36 world units. Spawn center spacing is 2.02 radii (0.3939 units), leaving about 4 mm between circles. Soft separation acts only inside a 2% band above each pair's summed radii; force is normalized within that short band so agents can still yield at contact. Hard swept circle contact, surface-constrained movement and bounded overlap correction remain active. A final overlap pass removes tiny penetrations from surface/float rounding within the same substep. No global separation slider is introduced.

`NavMeshConfiguration` uses world units and degrees:

| Parameter | Default | Purpose |
| --- | --- | --- |
| `cellSize` | 0.10 | Recast voxel resolution in X/Z; independent of debug terrain cells |
| `cellHeight` | 0.05 | Vertical voxel resolution and resulting height precision |
| `agentRadius` | 0.22 | Minimum boundary clearance; rounded up to whole voxels |
| `agentHeight` | 0.50 | Required headroom |
| `maximumSlope` | 45 | Maximum walkable surface angle in degrees |
| `stepHeight` | 0.15 | Maximum traversable vertical step; rounded down to voxels |

The table lists the generic builder defaults. The scene session overrides physical radius and height from each selected animal definition (chicken: 0.195 radius, 0.45 headroom). Separate cached profiles allow small animals to keep narrow routes when large animals coexist. Each profile still serves all matching enemies; there are no per-agent bakes. The generic spawn API rejects a mesh with insufficient radius or headroom. Voxel erosion, conservative rasterization and ledge filtering may close passages near the minimum physical width. Reduce voxel size when appropriate, within the build budget. Voxel/build parameters remain configured in code; animal dimensions and movement settings are configured in `assets/config/animals.json`. There is no global separation/radius slider.

Each profile uses one mesh, a 16-million-column raster budget, at most 32,767 columns per axis and the Recast vertical span limits. The baked radius may occupy at most 127 voxels to avoid overflowing Recast's byte-sized erosion distances. Queries use 4,096 Detour search nodes and at most 512 corridor polygons/corners. Local movement has a 64-polygon visited buffer and relies on bounded substeps. Invalid inputs, unsupported dimensions, failed builds and exhausted queries stop safely and report status. There are no background builds, streaming, off-mesh links, physics, combat or automatic decorative-model obstacles.

## Geometry changes

Phase 16 adds world-space tower obstacles through the same geometry source. Valid placement installs prepared candidate meshes at stable addresses; tower removal or scene edits rebuild profiles once. Enemy revisions invalidate their cached corridors before movement. Tower placement preserves configured spawn routes and reachable individual destinations. Grid cell data is unchanged; see [tower placement](towers.md) for controls, architecture and checks.

The scene session compares the terrain geometry source's revision. An unchanged scene reuses its profiles every frame. Switching animal selection does not rebuild existing profiles or modify existing animals. Wall changes rebuild the requested profiles on the next Game update; multiple changes before that update share one rebuild. The revision is only a dirty signal: navigation queries and steering never read grid cells or walkability values. A rebuild or reset, including a failed build, increments the NavMesh revision and discards native data. Enemies stop or obtain fresh corridors before using polygon references again. Scene replacement and terrain resizing retain the existing preview cleanup behavior.

## Verification

Phase 15 complete Debug and Release builds passed with Editor and tests enabled. Full CTest runs passed **21/21** in both configurations (Debug 141.81 seconds, Release 61.87 seconds). Vulkan validation and synchronization validation were enabled; neither final runtime log reported validation errors or synchronization hazards. All 16 C++ files changed in this phase passed clang-format verification, and `git diff --check` passed. Build/test logs and the formatted-file manifest are in `out/phase15/`.

The existing throughput scenarios still complete: 100 animals through the corridor, 500 across open ground, and all 100 through the authored chicken map. The authored map retains its 75.88 simulated seconds and 333 complete-path queries. New checks cover independent Idle/Reached playback and shared retargeting without replacing other animals' goals during ordinary frame updates.

- `Navigation.NavMesh`: geometry independent of grid cells, continuous projection, direct/funnel routes, blocked wall tops and buried floors, exact boundary handling, radius/headroom/slope/step settings, disconnected regions, invalid inputs, failed builds and revision invalidation.
- `Enemies.Definitions`: installed animal catalog, default chicken, per-type dimensions, missing files, duplicate models, invalid settings and invalid paths.
- `Enemies.Ai`: all four states, independent destinations, configurable stopping distance, retained arrival, retargeting, invalid goals, bounded unreachable retries, immediate revision recovery, missing navigation, physical obstruction and recovery after removing it.
- `Enemies.Crowd`: 15/30/60/144 FPS consistency, arbitrary-angle direct movement, braking/arrival, cached and unreachable paths, rebuilds, compact parallel movement, mixed-radius navigation through size-limited passages, overlap recovery, frame spikes, 100 agents through a corridor, 500 on open terrain, and all 100 through the authored chicken map's narrow openings. These throughput fixtures explicitly enable despawning to keep the exit clear.
- `Enemies.Runtime`: actual ImGui model selection, enemy inspection/goal clearing and mixed spawns, all 11 rendered animal assets and footprint coverage, default-hidden and toggleable NavMesh overlays, explicit/automatic profile rebuilds, 100/500 rendered chickens, wall edits, shared resource lifetimes, independent physical radius/visual scale, automatic walking animation selection, Idle/Reached animation pausing, independent retargeting, existing-instance stability and missing geometry.
- Existing Engine, camera, picking, terrain serialization, model, renderer and Editor checks remain enabled.

The complete project built successfully in Debug and Release. Both full CTest suites passed **20/20** (Debug 139.51 seconds, Release 62.54 seconds), including rendering every configured animal. Runtime checks ran with Vulkan validation and synchronization validation enabled; neither log reported validation errors or synchronization hazards. All 13 added/modified C++ files passed the root clang-format verification, and `git diff --check` passed.

The deformation-arena follow-up reproduces the previous fatal error with a default batch of 100 Black Rats: 235 joints were uploaded three times per instance, exceeding the old 65,536-matrix arena. Joint palettes and morph weights now upload once per instance node and are reused across its primitives. Draw records remain per primitive. Frame buffers grow geometrically when needed, retain capacity after clearing, and remain bounded by the device's storage-buffer range and shader index limits. Replacement buffers and descriptor bindings are updated only after the existing frame fence has completed. The separate 4,096-set pool limits unique uploaded GPU primitives, not the number of rendered instances. Scene preflight uses the same device limits and per-node accounting.

Regression checks cover 100/300 actual rats, 200 actual bunnies, a synthetic rendered frame with 5,000 primitive records and 235,000 matrices, growth past 65,536 morph weights, descriptor release/reuse, empty frames and invalid inputs. Morph-count mismatches now have their own diagnostic rather than sharing the capacity error. Complete follow-up builds passed, with **20/20 CTest checks passing in both Debug and Release** (140.12 and 62.30 seconds respectively), no Vulkan/synchronization validation errors, and formatting/whitespace checks passing. Follow-up build/test logs are under `out/deformation-fix/`.

In the Release checks, all 100 compact chickens completed the authored narrow-passage map in 75.88 simulated seconds, with 333 complete-path queries across the run. Separate open-floor and corridor checks also completed, including 500 agents. Over 120 rendered frames, average enemy CPU time was 0.36 ms for 100 chickens and 2.30 ms for 500; median full-frame times were 4.17 and 7.63 ms respectively. These are local measurements with validation enabled. Logs are in `out/animals/`.

## Changed files

Phase 15 adds `game/src/systems/enemy_ai.{hpp,cpp}` and `tests/enemy_ai.cpp`. It updates the enemy entities/definitions, `systems/{enemy_system,crowd_movement}.{hpp,cpp}`, `navigation/navigation_session.cpp`, `debug/navigation_debug.{hpp,cpp}`, Game/tests CMake lists, animal-definition/crowd/runtime tests, README and this document. Engine, Editor, navigation algorithms, rendering and third-party sources are unchanged by this phase.

For the animal-spacing and selection update: added `game/assets/config/animals.json`, `game/src/entities/enemy_definition.{hpp,cpp}`, and `tests/animal_definition.cpp`. Updated `game/CMakeLists.txt`, `game/src/entities/enemy.hpp`, `game/src/systems/{enemy_system,crowd_movement}.{hpp,cpp}`, `game/src/navigation/navigation_session.{hpp,cpp}`, `game/src/debug/navigation_debug.cpp`, `tests/{CMakeLists.txt,enemy_crowd.cpp,enemy_runtime.cpp}`, `README.md` and this document. Those changes left Engine, Editor and Recast/Detour implementations unchanged.

The deformation-arena follow-up updates `engine/src/core/engine.cpp`, `engine/src/renderer/{deformation,vulkan_renderer}.{hpp,cpp}`, `tests/{renderer_runtime,enemy_runtime}.cpp` and this document. No shaders, animal settings, navigation algorithms or external dependencies changed for that fix.

The earlier Phase 14 integration changed these files:

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
