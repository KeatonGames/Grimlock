# Grimlock

## Project Overview

Grimlock is a C++20 tower defense project using SDL3 and Vulkan. **Grimlock Game** and
**Grimlock Editor** are separate applications sharing Engine and GameLogic. Game
loads handcrafted levels and provides its own HUD; Editor supplies scene authoring
and development tools. Game does not link Editor or GameDebug.

## Features

- **Level 01 - Forest Defense:** a handcrafted Kenney woodland with two elevations,
  strategic build areas, five fixed waves and an Iron Stag finale; see [the level guide](docs/forest_defense.md).
- Vulkan 1.3+ Dynamic Rendering and Synchronization2, with window resizing and deterministic cleanup.
- OBJ and glTF/GLB loading, textured PBR materials, punctual lights, image-based lighting and HDR tone mapping.
- Shared model resources with independent skeletal, node and morph animation playback per instance.
- JSON scenes with stable object IDs, parent-child transforms, cameras, lights and initial animation settings.
- Mouse camera navigation, model picking and selection highlighting.
- Scene editor with model placement, grouping, duplication/deletion, transform editing, scene saving/loading and animation controls.
- Optional XZ navigation grid with configurable dimensions/origin/cell size, walkability painting, revision tracking and scene serialization.
- Imported 3D terrain with elevation-aware Recast navigation, tower placement and combat; see [terrain authoring](docs/terrain.md).
- One tower per Editor-authored construction zone, with fixed construction points and facing; see [build zones](docs/build_zones.md).
- Terrain-aware targeting and swept projectile collision, with optional Editor visibility rays; see [combat and cover](docs/combat.md).
- Data-driven Goblin, Siege and Hunter targeting policies and a four-wave elevated scenario; see [enemy archetypes](docs/enemy_archetypes.md).
- Boss definitions reuse enemy combat/navigation and add stacked Game HUD health bars; see [boss encounters](docs/bosses.md).
- Main menu and automatic discovery of Editor-authored levels, with in-game switching and restart; see [level selection](docs/level_selection.md).
- Persistent attempts, victories, completion records and last played in user data; see [progression](docs/progression.md).
- Responsive Game HUD, shared visual components, contextual command cards and pause/settings; see [Game UI](docs/game_ui.md).

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
  assets/config/           Shared enemy, hero and tower definitions
  assets/levels/           Handcrafted levels, waves and economy settings
  assets/environment/      Nature, castle, dungeon, graveyard and pirate GLBs
  assets/towers/           Structures, weapons and projectile GLBs
  assets/enemies/          Animal, orc, undead and other enemy GLBs
  assets/heroes/           Knight, Ranger, Mage, other characters and shared rigs
  assets/licenses/         Original license texts and provenance notices
  assets/scenes/           Authored environments and diagnostic scenes
tests/                      CPU and Vulkan/editor runtime checks
docs/                       Current gameplay, authoring and architecture guides
  archive/                  Completed refactoring plan and historical project notes
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

# Open the main menu and choose a level
.\out\build\debug\Debug\Grimlock.exe

# Scene editor
.\out\build\debug\Debug\GrimlockEditor.exe

# Open the handcrafted forest directly
.\out\build\debug\Debug\Grimlock.exe assets/levels/forest_defense.json
.\out\build\debug\Debug\GrimlockEditor.exe --level assets/levels/forest_defense.json
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

CMake compiles shaders automatically and synchronizes assets beside each executable, pruning obsolete staged files. GLB files must contain actual model data, not Git LFS pointer text.

Game discovers all valid JSON levels under its active `assets/levels` directory.
Use `--assets <folder>` to play directly from an authored asset tree; see
[shared Editor/Game paths and live level reloads](docs/level_selection.md).

Run tests after building:

```powershell
ctest --test-dir out/build/debug -C Debug --output-on-failure
ctest --test-dir out/build/release -C Release --output-on-failure
```

Runtime tests require a working Vulkan display and the supplied assets.

## Repository housekeeping

Keep authored assets in `game/assets`, checks in `tests`, and generated output in
the ignored `out` directory. The GLB-only asset library is described in [asset organization](docs/asset_library.md). Diagnostic scenes and sample models also serve as
regression fixtures; retain them even when no playable level references them.

Local output is organized into `out/build` for active CMake builds,
`out/dependencies` and `out/downloads` for dependency caches, `out/cleanup` for the
latest cleanup verification, and `out/archive` for historical logs and captures.
Archived results describe earlier revisions and are not current verification.
Old generated build trees can be removed when unused; check CMake cache source
overrides before removing dependency folders.

The [historical reports](docs/archive/history.md) and
[completed refactoring plan](docs/archive/refactoring.md) are retained separately
from current guides.

For Editor profiling, reproducible combat stress tests, and measured results, see
[combat performance](docs/combat_performance.md).
The [performance and stability report](docs/performance_stability.md) covers the
Game HUD benchmark, picking optimization, crowd-hash regression, and stall recovery.

For ECS ownership, generation handles, mutation rules, migration scope, and checks,
see [Gameplay ECS](docs/ecs.md). The [ECS validation report](docs/ecs_validation.md)
records lifetime regressions, allocation measurements, stress results, and remaining limits.

Hero/tower ownership, Scene bridges, and migration verification are documented in
[ECS heroes and towers](docs/ecs_heroes_towers.md).

For Knight, Ranger, and Mage deployment, models, and combat configuration,
see [hero types](docs/hero_types.md).

See [gameplay controls](docs/gameplay_controls.md) for selection, orders, cancellation,
and interaction priorities. Camera controls remain right-drag pan, middle-drag orbit, and wheel zoom.

