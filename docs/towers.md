# Tower placement

Placement uses required build zones, data-driven Small/Large tower types and a
preparation-only placement gate. See [build zones and placement](build_zones.md)
for current controls, persistence and checks.

Both types now attack during level Battles. See [tower combat](combat.md) for
statistics, projectiles, enemy health/death and gameplay range indicators.
The existing `basic`/`wide` type IDs and physical footprints remain compatible.

Open `assets/scenes/debug.json` in Editor mode, or load First Watch through the Level menu. Other terrain scenes need build zones created first. Use **Navigation > Towers > Place Tower**, then move the mouse over the terrain. Left-click places a tower; placement remains active for additional towers. Escape or **Cancel Placement** exits. Mouse pan, orbit and zoom remain available. Placement owns selection clicks and temporarily takes priority over walkability painting.

The transparent box follows a world-space mouse ray without snapping to grid cells. Footprint/support/overlap checks run immediately on every cursor update, without a timer or NavMesh rebuild. Green means the footprint is clear; red means a footprint conflict or a route rejection from the last click at that position. Every click runs the full route and current-animal clearance checks before committing. A clear footprint can still be rejected if it would seal a route; the tooltip explains why. UI clicks, camera gestures and off-terrain rays cannot place towers.

After cancelling placement, select a tower normally in the viewport or hierarchy. Delete it using the existing editor command/confirmation, or **Navigation > Towers > Remove Selected Tower**. Saving a scene preserves its towers; loading restores their obstacles. Renaming towers does not change their identity.

## Data and ownership

- `entities/tower.hpp` contains scene ID, position, rotation, model reference and physical half-extents. `systems/tower_system.{hpp,cpp}` handles scene synchronization, shared placeholder creation, footprint checks, terrain ray intersection and spawn-area route queries.
- Towers are ordinary selectable scene objects using the existing `models/cube.obj` resource. Engine shares their model/material/GPU resources. A translucent preview is drawn through the existing Editor world-projection overlay, without creating preview scene objects or Vulkan resources. Like other debug overlays, it does not use depth occlusion.
- The initial placeholder is 0.9 × 1.5 × 0.9 world units. Its physical footprint is defined in Game and remains independent of model bounds and editor visual scale. Rotation produces conservative world-space box bounds. Placement creates upright towers; existing scene transform controls remain available afterward.
- Scene objects have an optional, generic `role` string, preserved by loading, saving and duplication. Game recognizes `grimlock.tower`; Engine and Editor do not interpret tower roles. Existing scenes omit the field and continue loading unchanged.
- Scene owns placed models. TowerSystem keeps ID-based snapshots, not owning pointers to scene objects or separate GPU resources. Deletion, hiding, transform changes and scene replacement update the snapshots. Runtime enemies remain independent of authored tower objects.

## Placement and navigation

The whole footprint must rest on terrain and avoid walls, towers, ordinary scene model bounds and animal bodies. Model bounds are conservative placement exclusions; decorative props retain their existing navigation behavior. Placement also rejects removing baked navigation clearance underneath existing animals.

`NavigationSession` appends tower solid boxes to the existing world-space navigation geometry. It does not modify grid cells or introduce another navigation algorithm. Terrain/grid revisions and TowerSystem revisions signal geometry changes; unchanged frames reuse the live NavMeshes.

Route validation runs on click, building temporary candidates only for the selected navigation profile and profiles currently used by animals. It preserves at least one spawn-area-to-shared-destination route for each of those profiles. Previously inspected but unused large animals do not require extra bakes or prevent chicken-sized layouts. A reachable configured spawn area and destination are required before placement. Existing reachable individual enemy destinations are protected too; animals already unreachable retain their normal FSM handling.

The spawn area is intersected with actual NavMesh polygon triangles, and Detour checks routes from the resulting world-space points. This checks continuous regions, including very small/point spawn areas, rather than sampling grid cells or assuming the rectangle center is reachable. Existing endpoint projection rules remain in effect, and a tower cannot remove the projected destination.

Click-time candidate meshes are cached by position, navigation revision, spawn area, destination and selected model. Cache coverage includes newly occupied animal profiles, so spawning another type cannot reuse an incomplete validation. On success, prepared data replaces each used live mesh's contents while retaining its address. Unused cached profiles are invalidated and rebuild on demand when selected again; they never retain pre-tower geometry. Revisions change, so existing enemy paths recalculate through the Phase 15 FSM before moving. Rejected placements never install temporary data.

Removal rebuilds the requested profiles once from the remaining geometry, restoring navigable space. Cancelling placement releases temporary candidate meshes. Direct scene/grid edits use the normal rebuild path; placement guards apply to the Place Tower tool rather than restricting the general-purpose scene editor.

## Verification

`Towers.Cpu` checks supported footprints, walls, animal overlap, rotated bounds, terrain ray depth, arbitrary positions, elevated/non-unit terrain, small continuous spawn regions, last-route closure, transactional mesh replacement and scene role serialization. `Enemies.Runtime` additionally exercises the real Place Tower button, green/red preview, click placement, Escape cancellation, normal tower picking, shared resources, renaming/save/load, both removal paths and enemy navigation around towers. It covers cached previews when a larger animal is spawned or removed.

Build and run all existing checks using the Debug/Release presets and CTest commands in README. Local verification logs are under `out/phase16/`.

Initial Phase 16 builds and CTest runs passed **22/22** in Debug and Release (150.82 and 68.47 seconds). Runtime checks ran with Vulkan validation and synchronization validation enabled, with no validation errors or synchronization hazards. Those checks confirm animals pass the tower corridor, reach their goal, and regain navigable space after editor deletion. Camera zoom also remains functional while placement is active.

The placement-speed follow-up removes the previous 100 ms wait and all hover-time route bakes. Runtime regressions require a green footprint on the first hover frame and on every moving-cursor frame, verify click-time rejection of the last-route closure, and verify on-demand rebuilding of unused animal profiles. A 1,000-position hover benchmark reports timing without an unstable wall-clock pass threshold. Follow-up logs are under `out/tower-placement-speed/`.

Follow-up Debug and Release builds passed. The complete Release suite passed **22/22** (67.16 seconds); Debug AI, NavMesh, tower CPU and enemy/editor runtime checks passed **4/4**, including the placement UI checks. Final runtime logs contain no Vulkan validation errors or synchronization hazards. The Release test map measured **0.5993 ms for 1,000 hover checks**. The five C++ files changed for this follow-up passed clang-format verification, and whitespace checks passed.

## Files

Added `game/src/entities/tower.hpp`, `game/src/systems/tower_system.{hpp,cpp}`, `tests/tower.cpp` and this document. Updated Game navigation/session/debug integration and CMake, the generic Editor viewport hook/box projection/document dirty notification, Scene role serialization/duplication, `tests/enemy_runtime.cpp`, tests CMake, README and the navigation documentation. Enemy movement algorithms, Vulkan rendering, external dependencies and grid serialization are unchanged.
