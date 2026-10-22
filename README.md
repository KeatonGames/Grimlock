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
.\out\build\debug\Debug\Grimlock.exe --editor
.\out\build\debug\Debug\Grimlock.exe --editor assets/scenes/demo.json
.\out\build\debug\Debug\Grimlock.exe --editor assets/scenes/navigation.json
.\out\build\debug\Debug\Grimlock.exe assets/models/blackrat.glb
.\out\build\debug\Debug\Grimlock.exe --help
```

Editor mode without a scene opens the navigation debug lab. Explicit JSON paths still open their chosen scene; add models through the hierarchy. Standalone OBJ/GLB/glTF preview uses Game mode and starts the first clip if available.

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

The default **debug lab** (`assets/scenes/debug.json`) has marked spawn/goal areas, wide and narrow gates, a tower-building plaza, two removable sample towers and an isolated pocket for Blocked-state checks. Its camera frames the whole layout. See the [debug scene guide](docs/debug_scene.md) for test scenarios and coordinates.

Open `assets/scenes/navigation.json` for a cube floor, blocked-cell walls and existing GLB props. Every grid cell has a blue-grey floor cube; blocked cells also have a taller orange wall. Painting walkability updates the geometry in the same frame. Engine instances the shared `models/cube.obj` asset from the configured asset root; it creates no per-cell mesh resources or scene objects. GLB props stay independent and do not automatically block cells.

- **Navigation > Edit Walkability** shows the grid overlay and enables left-drag painting. Green is walkable, red is blocked, and the hovered cell is highlighted. Choose **Paint Blocked** or **Paint Walkable**; disable editing to hide every grid line/highlight and restore normal picking. Camera controls remain available. Cube floors and walls stay visible in both modes.
- **Grid Settings...** changes X/Z dimensions, cell size and world origin; scenes without a grid offer **Create Grid...**. Overlapping cell coordinates retain their states when resized; shrinking discards cells outside the new bounds. The limit is 65,536 cells.
- **File > Save/Save As** saves the grid with the scene. As with other edits, save into the source scene directory to keep changes across builds.

Cell `(x,z)` spans `[origin.x + x*s, origin.x + (x+1)*s)` and the equivalent Z interval, where `s` is cell size. Its center is `origin + ((x+0.5)*s, 0, (z+0.5)*s)`. Y sets the floor surface and picking/overlay plane. Floor thickness is `0.2*s` below that plane; walls rise `1.5*s` above it. Generated cubes are not selectable and do not affect camera input. World conversion floors the X/Z offsets divided by `s`; out-of-bounds queries safely return no cell.

`grid.hpp` provides allocation-free coordinate/neighbor queries and a revision counter that advances only on changes. The optional scene `navigation_grid` stores `width`, `height`, `cell_size`, `origin: [x,y,z]` and sparse `blocked: [[x,z], ...]`; omitted cells are walkable. Existing scenes without it still load. Game scene data owns configuration and authored obstacles; Engine supplies generic storage, serialization and editor infrastructure.

### Chicken enemies and NavMesh navigation

Open `assets/scenes/chickens.json` with `Grimlock.exe --editor`. Choose **Navigation > Animal Model**, then **Spawn Animals**. Chicken is the default; all 11 configured animal GLBs are available. **Movement Speed** sets the selected type's speed for future spawns, leaving existing animals unchanged. **Destination X/Z** and **Clear Animals** operate on the whole crowd. Standalone Game mode spawns 100 chickens automatically on terrain scenes. Destinations remain continuous world coordinates.

Recast Navigation 1.6.0 builds shared NavMesh profiles from the same world-space cube geometry used by the renderer, caching each requested physical radius/headroom pair. Small animals retain narrow routes when larger animals are added. Detour caches polygon corridors per enemy, then funnel processing guides continuous movement around walls. Physical circles, separation, smoothed facing, model sharing and animation handling remain active. Floor boundaries and walls constrain movement; there is no unbounded fallback when navigation geometry is absent.

**Show NavMesh** enables an optional cyan debug overlay; it is hidden by default. **Rebuild NavMesh** rebuilds explicitly. Wall edits also rebuild automatically on the next movement update and invalidate cached paths. Grid lines remain visible only during **Edit Walkability**. Existing terrain/scene serialization is unchanged, and runtime chickens are not saved or selectable.

Animal dimensions, independent visual heights, movement and animation settings live in `game/assets/config/animals.json`. Chickens use a 0.195-unit radius, approximately 4 mm of spawn padding and a short 2% soft separation band; hard circular contact checks remain active. Different animal types share one crowd solver and their model resources. Add an animal GLB to `assets/models` and a matching definition to the JSON catalog; unrelated GLB props are excluded from the selector.

Enemies use a lightweight Idle/Moving/Reached/Blocked FSM with independent destinations, configurable stopping distance and bounded retries for unreachable or stuck movement. **Navigation > Inspect Enemy** shows each animal's state and lets you edit or clear its goal. **Destination X/Z** retargets all current animals and sets the default for future spawns. Animals disappear on arrival, releasing their render instances and physical footprints. Set `despawn_on_arrival` to false in an animal definition to retain reached animals for inspection.

See [NavMesh navigation](docs/navigation.md) for architecture, AI settings, controls, tests and the changed-file list. The chicken model has no animation clips and uses its rest pose; animated enemy configurations retain independent playback and pause when stationary.

### Tower placement

Use **Navigation > Towers > Place Tower** in a terrain scene. The transparent preview responds immediately: green means a clear footprint; red indicates an overlap or the last rejected placement. Left-click performs full route validation and places a shared cube placeholder if valid; Escape or **Cancel Placement** exits. Layouts that close the last spawn-to-destination route are rejected. Mouse camera controls remain available.

Towers are selectable scene objects and persist in saved scenes. Remove one through the hierarchy/Inspector delete command or **Remove Selected Tower**. Placing/removing towers updates Recast/Detour obstacles and invalidates enemy paths safely. See [tower placement](docs/towers.md) for architecture, validation and verification details.

### Build zones

Tower construction now requires a predefined build zone. Use **Navigation > Build
Zones** to create/edit rectangles, and **Navigation > Towers > Tower Type** to choose
Basic or Wide towers before placing freely inside them. First Watch and the debug
scene include zones. See [build zones and placement](docs/build_zones.md) for controls,
collision footprints and persistence.

### Handcrafted levels and waves

In the editor, choose **Navigation > Level > Load First Watch**. Place towers during
preparation, then click **Start Wave** in the Level panel. Three predefined waves
use exact animal counts, spawn areas and timing. Arrivals damage the objective and
disappear; surviving the final wave wins, and zero objective health loses.
**Restart Level** restores the authored scene and gameplay state. See
[levels and wave authoring](docs/levels.md) for configuration and prototype limits.

## Current Limitations

- Navigation currently builds small debug-terrain NavMeshes for requested animal dimensions. Decorative GLB props do not contribute obstacles automatically. There is no physics, tile streaming, off-mesh traversal or DetourCrowd. Physical radii conservatively cover each configured model's XZ footprint; large animals may not fit narrow openings. Tight spawn areas can fit fewer enemies than requested.
- No animation blending, animation state machines, timelines, keyframe editing or retargeting.
- No multi-selection, undo/redo, transform gizmos, material editor or asset hot reload. Editor layouts are session-only.
- Reparenting that requires shear is rejected rather than changing the model's appearance.
- Some supplied static skinned assets have incomplete rest-joint data and use a warned bind-pose preview. The Engine cannot reconstruct missing rig data or invent animation clips.
