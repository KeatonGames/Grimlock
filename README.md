# Grimlock

## Project Overview

Grimlock is a C++20 Engine/Game project using SDL3 and Vulkan. Engine owns loading, scenes, animation, input and rendering. Game supplies the demo assets and entry point; an optional Dear ImGui scene editor runs in the same executable.

## Features

- Vulkan 1.3+ Dynamic Rendering and Synchronization2, with window resizing and deterministic cleanup.
- OBJ and glTF/GLB loading, textured PBR materials, punctual lights, image-based lighting and HDR tone mapping.
- Shared model resources with independent skeletal, node and morph animation playback per instance.
- JSON scenes with stable object IDs, parent-child transforms, cameras, lights and initial animation settings.
- Mouse camera navigation, model picking and selection highlighting.
- Scene editor with model placement, grouping, duplication/deletion, transform editing, scene saving/loading and animation controls.
- Optional XZ navigation grid with configurable dimensions/origin/cell size, walkability painting, revision tracking and scene serialization.

## Tech Stack

| Purpose | Technologies |
| --- | --- |
| Language and build | C++20, CMake |
| Window and graphics | SDL3, Vulkan, Slang compiled to SPIR-V |
| Math and assets | GLM, fastgltf, simdjson, TinyObjLoader, stb_image, MikkTSpace |
| Scene files and editor | nlohmann/json, Dear ImGui docking with SDL3/Vulkan backends |

CMake downloads pinned dependencies through FetchContent. Pins live in `cmake/Dependencies.cmake` and `cmake/ImGui.cmake`; the Vulkan SDK supplies the shader compiler.

## Project Structure

```text
cmake/                      Dependency and shader build configuration
engine/
  include/grimlock/         Public Engine APIs, including the generic CPU grid
  src/                      Core, platform, input, camera, assets, scene,
                            animation and renderer implementations
  shaders/                  Slang shader sources
editor/                     Inspector, hierarchy and scene editing commands
game/
  src/entities/             Enemy IDs, shared definitions and runtime data
  src/systems/              Enemy lifecycle, transforms, animation and local crowd steering
  src/navigation/           Wall clearance utilities and scene movement session
  src/debug/                Game navigation/enemy ImGui controls
  src/{main.cpp,game.*}     Executable launcher and Game application
  assets/{models,scenes}/   Source models, scenes and Game navigation configuration
tests/                      CPU and Vulkan/editor runtime checks
docs/                       Historical project notes
out/                        Generated builds/runtime assets (Git-ignored)
```

## Requirements

- CMake 3.24+ and a C++20 compiler. Windows presets require Visual Studio 2026 with **Desktop development with C++** and an x64 toolchain.
- Vulkan SDK with `slangc`, plus a Vulkan 1.3+ loader/device supporting Dynamic Rendering, Synchronization2 and an sRGB swapchain.
- Git and Git LFS for the supplied GLB assets.
- Network access for the first dependency download.

Windows is the verified development environment. Other platforms need their own CMake generator, SDL3 build prerequisites and compatible Vulkan display/driver; they have not been validated here.

## Build & Run

Run these PowerShell commands from the repository root:

```powershell
git lfs pull
cmake --preset debug -DGRIMLOCK_BUILD_EDITOR=ON -DGRIMLOCK_BUILD_TESTS=ON
cmake --build --preset debug --parallel 4

# Demo Game
.\out\build\debug\Debug\Grimlock.exe

# Scene editor
.\out\build\debug\Debug\Grimlock.exe --editor
```

For Release:

```powershell
cmake --preset release -DGRIMLOCK_BUILD_EDITOR=ON -DGRIMLOCK_BUILD_TESTS=ON
cmake --build --preset release --parallel 4
.\out\build\release\Release\Grimlock.exe --editor
```

The executable target is `Game`; its output name is `Grimlock`. Editor support defaults to ON and can be excluded with `-DGRIMLOCK_BUILD_EDITOR=OFF`. Launching without `--editor` does not create editor panels.

CMake compiles shaders automatically and stages assets beside each executable. GLB files must contain actual model data, not Git LFS pointer text.

Run tests after building:

```powershell
ctest --test-dir out/build/debug -C Debug --output-on-failure
ctest --test-dir out/build/release -C Release --output-on-failure
```

Runtime tests require a working Vulkan display and the supplied assets.

## Usage

### Loading scenes and models

The default scene is `game/assets/scenes/demo.json`, staged as `assets/scenes/demo.json`. Command-line paths are absolute or executable-relative, not relative to the terminal's working directory:

```powershell
.\out\build\debug\Debug\Grimlock.exe --editor assets/scenes/demo.json
.\out\build\debug\Debug\Grimlock.exe --editor assets/scenes/navigation.json
.\out\build\debug\Debug\Grimlock.exe assets/models/blackrat.glb
.\out\build\debug\Debug\Grimlock.exe --help
```

Editor mode opens JSON scenes; add models through the hierarchy. Standalone OBJ/GLB/glTF preview uses Game mode and starts the first clip if available.

### Camera and selection

Use these controls over the 3D viewport, outside the editor panels:

| Input | Action |
| --- | --- |
| Right mouse + drag | Pan parallel to the XZ plane |
| Middle mouse + drag | Orbit around the target |
| Mouse wheel | Zoom |
| Left-click | Select a model; empty space clears selection |

There are no keyboard camera controls. UI interaction captures input instead of moving the camera or picking through panels.

### Scene editor

- **File > New/Open/Save/Save As** manages JSON scenes. An asterisk marks unsaved changes; close/open/new requests guard against losing them.
- **Add Model** opens an asset picker. Put new models under `game/assets/models/`, rebuild to stage them, then use **Refresh List**. New instances are fitted and placed at the camera target without changing imported geometry.
- **Create Group** creates an empty scene object. Right-click for Group Selected Object, Rename, Duplicate or Delete. Double-click hierarchy names to rename.
- Drag names onto groups to reparent; drop onto **Scene Root** or empty hierarchy space to unparent. World transforms are preserved when representable as translation/rotation/scale.
- The Inspector edits name, enabled state and local position/rotation/scale immediately. Drag numbers or Ctrl+click to type values. Scroll over the Inspector to reach its other properties.
- Duplicate copies one object's properties and authored animation configuration, not its children. Delete asks for confirmation and removes descendants too.

**Saving source scenes:** the editor normally opens the staged runtime copy. Builds overwrite staged assets. Use Open or Save As with the absolute path to your scene under `game/assets/scenes/` to preserve authored changes across builds.

### Animation controls

Select **Rat idle**, or a rat under **Rat placement group**, in the demo hierarchy. Their shared `blackrat.glb` contains 13 imported clips. The other supplied GLBs, including `assassin.glb`, have no animation clips; the Inspector reports this explicitly.

The Animation section provides the clip dropdown, Play/Pause/Resume, Stop, Loop, Autoplay and Playback Speed. Selection or shrinking the Inspector reveals clipped animation controls once; manual scrolling remains available. `_Static Pose` is intentionally static; choose a moving clip such as `idle_A1` to see motion.

- Changing clips resets time and updates the pose. Pause preserves it, Resume continues, and Stop restores rest pose. Non-looping clips hold their endpoint.
- Loop, Speed and Autoplay are saved per object. Autoplay applies when the scene loads, not when its checkbox is toggled.
- Preview buttons and elapsed playback time do not dirty the scene or change its saved configuration.
- Instances can use different clips and speeds while sharing immutable model resources.

New saves store `clip`, `autoplay`, `loop` and `speed`, using a unique imported clip name where possible. Numeric-clip scenes still load. Missing clips retain their saved choice and fall back to rest pose. Public playback/configuration APIs are declared in `engine/include/grimlock/scene.hpp`.

### Terrain and navigation grid

Open `assets/scenes/navigation.json` for a cube floor, blocked-cell walls and existing GLB props. Every grid cell has a blue-grey floor cube; blocked cells also have a taller orange wall. Painting walkability updates the geometry in the same frame. Engine instances the shared `models/cube.obj` asset from the configured asset root; it creates no per-cell mesh resources or scene objects. GLB props stay independent and do not automatically block cells.

- **Navigation > Edit Walkability** shows the grid overlay and enables left-drag painting. Green is walkable, red is blocked, and the hovered cell is highlighted. Choose **Paint Blocked** or **Paint Walkable**; disable editing to hide every grid line/highlight and restore normal picking. Camera controls remain available. Cube floors and walls stay visible in both modes.
- **Grid Settings...** changes X/Z dimensions, cell size and world origin; scenes without a grid offer **Create Grid...**. Overlapping cell coordinates retain their states when resized; shrinking discards cells outside the new bounds. The limit is 65,536 cells.
- **File > Save/Save As** saves the grid with the scene. As with other edits, save into the source scene directory to keep changes across builds.

Cell `(x,z)` spans `[origin.x + x*s, origin.x + (x+1)*s)` and the equivalent Z interval, where `s` is cell size. Its center is `origin + ((x+0.5)*s, 0, (z+0.5)*s)`. Y sets the floor surface and picking/overlay plane. Floor thickness is `0.2*s` below that plane; walls rise `1.5*s` above it. Generated cubes are not selectable and do not affect camera input. World conversion floors the X/Z offsets divided by `s`; out-of-bounds queries safely return no cell.

`grid.hpp` provides allocation-free coordinate/neighbor queries and a revision counter that advances only on changes. The optional scene `navigation_grid` stores `width`, `height`, `cell_size`, `origin: [x,y,z]` and sparse `blocked: [[x,z], ...]`; omitted cells are walkable. Existing scenes without it still load. Game scene data owns configuration and authored obstacles; Engine supplies generic storage, serialization and editor infrastructure.

### Chicken enemies and free movement

Open the chicken test scene:

```powershell
.\out\build\debug\Debug\Grimlock.exe --editor assets/scenes/chickens.json
```

Use **Navigation > Spawn Chickens** (default count 100), **Clear Chickens**, **Movement Speed**, and **Spawn Area (world X/Z)**. **Destination X/Z** accepts continuous world coordinates, including negative/fractional positions and positions outside the terrain. The initial destination is near the far corner of the scene. For a clear route on the spawn side of the walls, try `(-9.37, -3.29)`. Enemy count, arrival count, update time, neighbor checks and frame time/FPS remain in the same menu. Grid scenes spawn 100 chickens automatically in standalone Game mode. In the editor, spawning also works without a grid.

Enemies steer directly toward the destination using normalized X/Z vectors, acceleration/deceleration, delta-time integration and smoothed facing. Their existing Y position is preserved. They never query grid directions, cell centers, successors or reachability. Changing the destination affects the next update without any navigation rebuild. Movement stops within the physical radius of the world destination; the shared definition's existing `despawnOnArrival` option controls removal (enabled by default).

There is no pathfinder. Wall faces and rounded corners retain the swept-circle collision and wall-sliding checks. If the direct route is blocked, an enemy can stop against the wall; it will not calculate a route around it. Wall edits affect collisions immediately. Painting a wall under an enemy stops it until clearance returns. Only actual blocked cells are obstacles: the terrain rectangle is not an invisible boundary, and the grid's size/origin/cell size do not guide movement. Authored GLB props remain separate from wall collision.

Each shared `EnemyDefinition` supplies its physical `radius`, speed, acceleration/deceleration, health data and animation configuration. Instances copy their type's radius; the crowd solver supports different radii using each pair's summed footprints. Radius has no ImGui slider. Set it in the type definition instead. Visual size uses the independent `visualHeight` (0.36 world units by default); the chicken radius defaults to 0.22. No additional enemy types or collision shapes are implemented.

The existing spatial hash, local separation, contact sliding and bounded overlap recovery remain in `game/src/systems/`. Runtime data stays in `game/src/entities/`. The scene movement session and grid wall queries remain in `game/src/navigation/`; Game's editor controls stay in `game/src/debug/`. Engine and Editor have no enemy movement behavior. Updates retain bounded substeps: at most 0.25 seconds and 64 substeps after a stalled frame, discarding excess elapsed time.

All chickens share the cached `models/chicken.glb` mesh/material/texture/rig resources, with independent transforms and playback state. This asset has no animation clips and uses its rest pose. Animated configurations still select a requested movement clip or walk/run fallback, pause/resume with movement and retain independent playback. Runtime instances do not participate in picking or scene JSON, and clearing/replacing the scene releases their poses while retaining the shared Engine model cache.

The grid's existing JSON format, generated cube geometry and wall-painting tools are preserved. Grid lines, cell fills and hover highlights appear only during **Edit Walkability**. Destination, enemies and development controls remain session-only.

`Terrain.Clearance` verifies true circle footprints, corners, tangency, wall sliding, live wall changes and world movement beyond terrain bounds. `Enemies.Crowd` checks direct arbitrary-angle routes, arrival, braking, 15/30/60/144 FPS consistency, independence from empty grid configuration, wall blocking/removal, mixed radii, overlap recovery, frame spikes and crowds of 100/500. `Enemies.Runtime` exercises edit-only overlays, spawn/clear, world destinations, predefined radii independent of visual scale, model sharing/lifetimes, 100/500 rendered chickens, grid-free movement and animations. Existing camera, picking, terrain serialization, renderer and editor regression checks remain enabled.

Verification: complete Debug and Release builds passed, and all 19 CTest checks passed in each configuration. Runtime checks ran with Vulkan validation and synchronization validation enabled, with no validation errors reported.

Changed files for this task:

| Change | Files |
| --- | --- |
| Removed | Old field algorithm and waypoint agent sources/headers, obsolete field scenario and algorithm tests |
| Updated | `game/src/entities/enemy.hpp`, `game/src/systems/{crowd_movement,enemy_system}.{hpp,cpp}` |
| Updated | `game/src/navigation/{navigation_session,grid_clearance}.{hpp,cpp}`, `game/src/debug/navigation_debug.{hpp,cpp}`, `game/src/game.cpp`, `game/CMakeLists.txt` |
| Updated | `editor/include/grimlock/grid_debug.hpp`, `editor/src/grid_tools.{hpp,cpp}`, `editor/src/editor.cpp` |
| Updated | `tests/enemy_crowd.cpp`, `tests/editor_runtime.cpp`, `tests/CMakeLists.txt`, `README.md` |
| Replaced/renamed | Field tests became `tests/grid_clearance.cpp`; field runtime checks became `tests/enemy_runtime.cpp` |

## Current Limitations

- Enemies move directly toward one world destination with local separation and wall contact handling. There is no obstacle routing, height sampling, automatic collision with GLB props or physics. Blocked routes and oversized circles may stop against walls. Tight spawn areas can fit fewer enemies than requested; type radii should cover their fixed visual model footprint.
- No animation blending, state machines, timelines, keyframe editing or retargeting.
- No multi-selection, undo/redo, transform gizmos, material editor or asset hot reload. Editor layouts are session-only.
- Reparenting that requires shear is rejected rather than changing the model's appearance.
- Some supplied static skinned assets have incomplete rest-joint data and use a warned bind-pose preview. The Engine cannot reconstruct missing rig data or invent animation clips.
