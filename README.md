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
  include/grimlock/         Public Engine APIs
  src/                      Core, platform, input, camera, assets, scene,
                            animation and renderer implementations
  shaders/                  Slang shader sources
editor/                     Inspector, hierarchy and scene editing commands
game/
  src/                      Executable launcher and Game code
  assets/{models,scenes}/   Source models and JSON scenes
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

## Current Limitations

- No animation blending, state machines, timelines, keyframe editing or retargeting.
- No multi-selection, undo/redo, transform gizmos, material editor or asset hot reload. Editor layouts are session-only.
- Reparenting that requires shear is rejected rather than changing the model's appearance.
- Some supplied static skinned assets have incomplete rest-joint data and use a warned bind-pose preview. The Engine cannot reconstruct missing rig data or invent animation clips.
