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
  src/                      Executable launcher and Game code
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

- **Navigation > Show Grid** toggles the session-only overlay: green is walkable, red is blocked.
- **Navigation > Edit Walkability** enables left-drag painting. Choose **Paint Blocked** or **Paint Walkable**; the menu bar shows the active mode. Disable editing to restore normal model picking. Camera controls remain available.
- **Grid Settings...** changes X/Z dimensions, cell size and world origin; scenes without a grid offer **Create Grid...**. Overlapping cell coordinates retain their states when resized; shrinking discards cells outside the new bounds. The limit is 65,536 cells.
- **File > Save/Save As** saves the grid with the scene. As with other edits, save into the source scene directory to keep changes across builds.

Cell `(x,z)` spans `[origin.x + x*s, origin.x + (x+1)*s)` and the equivalent Z interval, where `s` is cell size. Its center is `origin + ((x+0.5)*s, 0, (z+0.5)*s)`. Y sets the floor surface and picking/overlay plane. Floor thickness is `0.2*s` below that plane; walls rise `1.5*s` above it. Generated cubes are not selectable and do not affect camera input. World conversion floors the X/Z offsets divided by `s`; out-of-bounds queries safely return no cell.

`grid.hpp` provides allocation-free coordinate/neighbor queries and a revision counter that advances only on changes. The optional scene `navigation_grid` stores `width`, `height`, `cell_size`, `origin: [x,y,z]` and sparse `blocked: [[x,z], ...]`; omitted cells are walkable. Existing scenes without it still load. Game scene data owns configuration and authored obstacles; Engine supplies generic storage, serialization and editor infrastructure.

### Flow Field navigation prototype

Build with `GRIMLOCK_BUILD_EDITOR=ON` and open the corridor scenario:

```powershell
.\out\build\debug\Debug\Grimlock.exe --editor assets/scenes/flow_field.json
```

The existing **Navigation** menu contains **Destination X/Z**, **Show Flow Directions**, **Show Integration Costs**, **Agent Speed**, **Spawn Test Agents**, and **Reset Test Agents**. The gold ring marks the destination. Cost colors run from green (near) to orange (far); grey cells are unreachable. **Show Grid** also hides these debug overlays. Spawn creates up to six green cube instances with different speeds; Reset removes them. Speed is in world units per second, with per-agent multipliers configured in `game/src/navigation/navigation_demo.cpp`. Running this scene without `--editor` spawns the agents automatically and uses the last grid cell as the destination.

Game's `FlowField` runs reverse Dijkstra from the shared destination. Cardinal steps cost 1 cell, diagonals cost sqrt(2), and a diagonal requires both adjacent cardinal cells to be walkable. Blocked/unreachable cells have infinite integration cost and zero direction. A contiguous cost/direction/successor field and reusable heap serve every agent; unchanged frames only compare the grid pointer, revision and destination. Grid edits, resizing, scene replacement or a new destination rebuild it once. Invalid or blocked destinations stop agents safely.

Agents follow the field's safe center-to-center successors, consuming a speed-times-delta distance budget across as many edges as needed. Swept X/Z clearance accounts for the small cubes' rotated footprint and prevents crossing walls or blocked corners, even with a long frame. Only facing rotation is smoothed; routes do not round blocked corners. Grid changes retain a safe downhill waypoint where possible, otherwise recenter before taking the new route. If a wall is painted under/against an agent, it stops in place until there is clearance again; it is not teleported out of the edited wall. There is no physics or agent-to-agent avoidance.

Navigation calculation, movement, prototype configuration and ImGui controls live in `game/src/navigation/`. Editor receives non-owning field views through `editor/include/grimlock/grid_debug.hpp` and reuses the grid overlay. Engine supplies a generic update callback after viewport tools and before draw preparation, plus scene-visible runtime model instances. These instances share the existing cube asset, do not participate in picking or serialization, and are removed on Reset or scene/grid replacement. Authored grid walls retain the existing JSON format; destination, agents and visualization settings are session-only.

`Navigation.FlowField` checks costs, corner rules, coordinate conversions, revision caching, maximum grid size, shared movement, dynamic obstacles, arrival and 15/30/60/144 FPS consistency. `Navigation.Runtime` exercises the actual editor controls, coexistence with animated GLBs, runtime instances, wall updates, destination changes, reset and scene save/load.

Phase 11 implementation files:

| Status | Files |
| --- | --- |
| Added | `game/src/navigation/flow_field.hpp`, `flow_field.cpp`, `agent.hpp`, `agent.cpp` |
| Added | `game/src/navigation/navigation_demo.hpp`, `navigation_demo.cpp`, `navigation_debug.hpp`, `navigation_debug.cpp` |
| Added | `editor/include/grimlock/grid_debug.hpp`, `game/assets/scenes/flow_field.json` |
| Added | `tests/flow_field.cpp`, `tests/flow_field_runtime.cpp` |
| Modified | `engine/include/grimlock/engine.hpp`, `engine/src/core/engine.cpp` |
| Modified | `editor/include/grimlock/editor.hpp`, `editor/src/editor.hpp`, `editor/src/editor.cpp`, `editor/src/grid_tools.hpp`, `editor/src/grid_tools.cpp` |
| Modified | `game/src/game.hpp`, `game/src/game.cpp`, `game/src/main.cpp` |
| Modified | `game/CMakeLists.txt`, `tests/CMakeLists.txt`, `README.md` |

## Current Limitations

- Navigation is a shared-destination movement prototype. There is no height sampling, automatic obstacle detection from GLBs, physics, agent avoidance, enemy behavior or combat. Navigation obstacles come only from the authored walkable/blocked grid.
- No animation blending, state machines, timelines, keyframe editing or retargeting.
- No multi-selection, undo/redo, transform gizmos, material editor or asset hot reload. Editor layouts are session-only.
- Reparenting that requires shear is rejected rather than changing the model's appearance.
- Some supplied static skinned assets have incomplete rest-joint data and use a warned bind-pose preview. The Engine cannot reconstruct missing rig data or invent animation clips.
