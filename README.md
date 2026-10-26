# Grimlock

## Project Overview

Grimlock is a C++20 tower defense project using SDL3 and Vulkan. **Grimlock Game** and
**Grimlock Editor** are separate applications sharing Engine and GameLogic. Game
loads handcrafted levels and provides its own HUD; Editor supplies scene authoring
and development tools. Game does not link Editor or GameDebug.

## Features

- Vulkan 1.3+ Dynamic Rendering and Synchronization2, with window resizing and deterministic cleanup.
- OBJ and glTF/GLB loading, textured PBR materials, punctual lights, image-based lighting and HDR tone mapping.
- Shared model resources with independent skeletal, node and morph animation playback per instance.
- JSON scenes with stable object IDs, parent-child transforms, cameras, lights and initial animation settings.
- Mouse camera navigation, model picking and selection highlighting.
- Scene editor with model placement, grouping, duplication/deletion, transform editing, scene saving/loading and animation controls.
- Optional XZ navigation grid with configurable dimensions/origin/cell size, walkability painting, revision tracking and scene serialization.
- Imported 3D terrain with elevation-aware Recast navigation, tower placement and combat; see [terrain authoring](docs/terrain.md).

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
editor/                     Inspector, hierarchy, scene/level editing and gameplay debug tools
game/
  src/entities/             Enemy IDs, shared definitions and runtime data
  src/systems/              Enemy lifecycle, transforms, animation and local crowd steering
  src/navigation/           Wall clearance utilities and scene movement session
  src/runtime/              Independent playable level state and player placement
  src/ui/                   Player HUD without Editor dependencies
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

# Play First Watch
.\out\build\debug\Debug\Grimlock.exe

# Scene editor
.\out\build\debug\Debug\GrimlockEditor.exe
```

For Release:

```powershell
cmake --preset release -DGRIMLOCK_BUILD_EDITOR=ON -DGRIMLOCK_BUILD_TESTS=ON
cmake --build --preset release --parallel 4
.\out\build\release\Release\Grimlock.exe
.\out\build\release\Release\GrimlockEditor.exe
```

Targets `Game` and `Editor` produce `Grimlock` and `GrimlockEditor`. Build either with
`cmake --build --preset debug --target Game` or `--target Editor`. Editor defaults
to ON; configure with `-DGRIMLOCK_BUILD_EDITOR=OFF` for a Game-only build. The HUD
still works in that build. Both applications use the same staged asset directory.

CMake compiles shaders automatically and stages assets beside each executable. GLB files must contain actual model data, not Git LFS pointer text.

Run tests after building:

```powershell
ctest --test-dir out/build/debug -C Debug --output-on-failure
ctest --test-dir out/build/release -C Release --output-on-failure
```

Runtime tests require a working Vulkan display and the supplied assets.

