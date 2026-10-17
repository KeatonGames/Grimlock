# Development history

Historical verification reports, measurements, and file inventories from earlier phases. Paths, checkout limitations, and toolchain details below describe those phases, not the current repository layout; see the [README](../README.md#structure-and-ownership) for current organization and setup.

### Phase 7 validation / current checkout limitation

Debug and Release builds succeed. `Camera.Navigation`, `Input.State`, `Camera.Runtime`, and `Scene.Cpu` pass in both configurations. Camera tests cover initial pose preservation, XZ pan, 10,000 orbit updates without radius drift, pitch/zoom/ground bounds, fractional/flipped wheel input, unmapped keys/left button, drag edges, focus/resize/disable cancellation, cursor-mode teardown, active-camera switching, and attached-camera local/world save/reload. SDL events are injected into the Engine's own window for the integration checks; no keyboard camera actions or selection code is added.

The unfiltered suite currently has two asset-setup failures (`Engine.PublicApi`, `Scene.Runtime`), because source `blackrat.glb` is an LFS pointer rather than model bytes. Both checks also pass when run directly against an ignored local fixture root containing a cached blackrat copy whose SHA-256 matches the pointer: `7b1012b36cb510a3f96f8647242eecb25dc7c1e8234176084d01f0e6e53b6996`. Source assets/pointers are left untouched. Full default-scene verification needs the LFS models and the separately supplied `base.glb`; do not mistake this checkout condition for a camera/parser regression. Local test logs are `out/verification/phase7-*.log`.

An ignored diagnostic scene using available cached base/blackrat assets passes Debug and Release animated rendering, wide/tall/repeated resize, minimize/restore, runtime-path and cleanup checks. The supplied `witch.obj` preview passes stationary rendering with navigation enabled. Stationary capture comparison excludes the window's rounded DWM corners; no rendered interior pixels changed. Core and synchronization validation report no errors/warnings in these checks. The default JSON/model binaries are not replaced or edited to bypass the LFS setup requirement.

Earlier rendering/scene checks, recorded on 17 October 2026 before the current missing-LFS-assets checkout condition:

- Debug and Release configure, compile, and link successfully; generated SPIR-V validates for Vulkan 1.3.
- `Engine.PublicApi` passes canonical-path resource reuse, metadata, independent transformations/playback, paused versus advancing instances, automatic updates, invalid IDs, and RAII shutdown. Its graphical smoke mode also passes resize/minimize/restore. Game's generated compile command exposes neither private engine nor SDL/Vulkan headers.
- `Scene.Cpu` passes hierarchy propagation/cache behavior, reparenting/cycle rejection, stable IDs and cascading deletion, world-space camera/light detachment, defaults, Unicode/escaped strings, semantic JSON save/reload, and invalid version/type/reference/TRS/light/camera/property cases.
- `Scene.Runtime` passes resource sharing, paused/advancing instances, live playback save/reload, failed asset/clip/material activation rollback, disabled-parent rendering, external CPU-reference retention, and safe unload. Sixty-five 64-primitive load/render/unload cycles allocate 4160 deformation descriptor sets cumulatively (pool capacity 4096), proving freed sets/slots can be reused. Imported directional/point/spot lights render alongside scene point/spot lights; combined overflow is rejected without replacing the active scene.
- Frozen JSON visual variants use the same executable without recompilation: a disabled parent removes its rat, a changed cat position moves the rendered model, and all-disabled objects leave zero sampled geometry pixels (excluding rounded window corners). Default JSON Debug/Release scenes retain the previous 3312-sample geometry coverage and changing animation frames.
- CPU checks compare GLB with external/embedded glTF geometry and bounds, exercise multiple primitives/shared references, parent/TRS/matrix composition, transform-only nodes, default/first/no-scene selection, sparse/strided/offset accessors, normalized uint16 UVs, uint8/16/32 indices, non-indexed geometry, missing attributes, and OBJ cube/witch loading.
- Negative imports reject invalid children/cycles/scenes, byte ranges/counts, attribute counts, topology/extensions, indices, truncated buffers, sparse references, empty geometry, and missing files with path-specific diagnostics.
- Material/image checks cover external PNG/JPEG, percent-escaped filenames, data URI images, GLB buffer-view PNGs, RGBA alpha/orientation, RGB-to-RGBA conversion, per-primitive materials, glTF defaults, sampler mappings, UV1/texture transforms, missing/invalid images, and unsupported/missing UV sets. The former static-skin fixture now verifies preserved skin/animation data and initial framing with unchanged mesh-node transforms.
- PBR CPU checks cover all twelve supplied material images, imported/Mikk tangent handedness, mirrored/degenerate UVs, unchanged topology where possible, OBJ defaults, constant-radiance irradiance/GGX energy conservation, finite BRDF LUT values, cube direction normalization, HDR decoding, and bounded light packing.
- A dedicated GPU check now compares 24 patches to CPU references: prior PBR/texture/normal/AO/HDR-blend checks, plus morph-before-skinning, a second influence set, non-uniform skin normals, shared geometry with independent poses, unlit, emissive strength, zero specular, and clearcoat direct/IBL layering. Maximum RGB error is one 8-bit output value. The CPU BRDF reference was updated to the specification's maximum-channel diffuse energy rule.
- CPU animation checks verify blackrat's six images/material extensions, all 13 clips/235 joints, actual pose changes, different simultaneous clips, pause/resume, stop/restore/preserve, speed, looping/non-loop endpoints, quaternion slerp, STEP, cubic interval-scaled tangents/weights, hierarchy composition, and palette coordinate spaces. Every supplied GLB and OBJ imports successfully.
- A synthetic glTF verifies actual import of eight normalized influences, position/normal/tangent morph deltas, defaults and cubic animated weights, inverse binds, and directional/point/spot lights attached to animated nodes. It renders through the application with clean validation.
- Debug and Release animated scenes visibly show character/cat/bunnies plus both blackrats and spheres. Captures must change between frames for animation, while fixed-pose numerical captures remain stationary. Checks resize wide/tall, repeat resize requests, minimize/restore, launch from unrelated working directories, and shut down normally.
- A six-face synthetic Radiance HDR environment is decoded with values above 1 preserved, loaded through `--environment` from an unrelated working directory, and rendered with clean validation. Missing faces are rejected explicitly.
- Individual `witch.obj` and `hierarchy.glb` previews also pass stationary-frame, runtime-path, clean-shutdown, and core/synchronization validation checks with the PBR renderer.
- Core and synchronization validation are enabled during graphical checks; no Vulkan errors or warnings are reported. Validation also covers uploads, mip generation, material descriptors, and cleanup.
- Missing shaders during pipeline initialization, a missing model after renderer initialization, and a missing environment before renderer initialization each produce a specific error, exit 1, and an aborted startup profile, with no validation errors/warnings during cleanup.
- Debug emits one startup report; Release emits none. The default scene has the same measured geometry coverage (3312 sampled pixels) before/after; animation changes captures as expected. This is a regression check, not a claim of exhaustive pixel equivalence for all animations.

Local checks/artifacts are under ignored `out/verification/`: `animation-checks`, `scene-checks`, `pbr-checks`, `pbr-gpu`, and `animation-debug/release-*` captures/logs (the shared helper uses `material-gpu-*` for custom executables). They use current sources, the installed toolchain, and standard library; no test framework/system installation is required. Upstream GLM emits CMake deprecation warnings, and fastgltf's Release compiler flags emit a harmless `/Ob2` to `/Ob3` override warning on MSVC.

## Startup measurements and optimizations (architecture-refactor baseline)

Debug uses `std::chrono::steady_clock` spans, aggregated by phase with call count, inclusive milliseconds, and self milliseconds excluding nested spans. One report is emitted after the first successful/suboptimal `vkQueuePresentKHR` return; closing/failing earlier reports `aborted_before_first_present`. Total starts at Engine initialization, includes Game's scene setup, and ends at that first presentation return. It does not include OS executable loading/argument parsing or measure when the display actually shows the frame. Profiling stops after startup and emits nothing in Release.

Example output:

```text
[startup] result=first_present_returned total_ms=5086.196
[startup] phase="texture.decode.PNG_JPEG" calls=22 inclusive_ms=2392.534 self_ms=2392.534
```

Measurements below were taken before Phase 6: per-phase medians of three baseline and three final architecture-refactor Debug launches, on the toolchain/GPU above with the same default visible scene and core/synchronization validation enabled. They are historical measurements, not a new scene-system performance claim. Driver and filesystem caches were warm. Inclusive rows overlap and **must not be added**. These are host wall times, not CPU thread-time or GPU timestamps; `wait.*` rows identify host blocking. Upload measurements include CPU staging/command work and fence waits. Scene parsing/binding is additionally instrumented as `scene.read_parse_validate`/`scene.bind_resources`; lifecycle release waits use `wait.scene_resource_release.device_idle`.

| Phase | Before ms | After ms | Calls before -> after |
| --- | ---: | ---: | ---: |
| Total to first presentation return | 8860.491 | 5086.196 | 1 -> 1 |
| SDL/window | 121.055 | 110.480 | 1 -> 1 |
| Vulkan instance/device/surface | 98.438 | 100.091 | 1 -> 1 |
| Swapchain creation | 208.790 | 180.137 | 2 -> 1 |
| Mesh pipelines (eight variants) | 3.189 | 3.037 | 1 -> 1 |
| Tone-map pipeline | 0.513 | 0.513 | 1 -> 1 |
| glTF file reads | 118.909 | 124.753 | 4 -> 4 |
| glTF parsing/external buffers | 167.134 | 173.518 | 4 -> 4 |
| PNG/JPEG decoding | 6017.015 | 2392.534 | 22 -> 22 |
| Buffer creation/allocation/mapping | 57.328 | 61.865 | 421 -> 384 |
| Mesh/buffer uploads | 87.716 | 68.680 | 60 -> 54 |
| Image creation/allocation | 6.212 | 6.381 | 28 -> 27 |
| Image uploads/mips | 383.808 | 222.901 | 26 -> 26 |
| Upload-fence host waits | 287.214 | 98.664 | 86 -> 80 |
| Animation/skin/light import preparation | 97.003 | 110.678 | 4 -> 4 |
| Instance pose preparation | 23.363 | 28.046 | 20 -> 23 |
| CPU environment preprocessing | 662.846 | 600.321 | 1 -> 1 |
| Complete GPU model uploads | 497.394 | 318.683 | 6 -> 5 |

Baseline totals: 9050.783, 8838.681, 8860.491 ms. Final totals: 5185.299, 5086.196, 5024.697 ms. Median improvement is **42.6% (8.86 s -> 5.09 s)**. Full diagnostic logs are under ignored `out/verification/startup-baseline-{1,2,3}.log` and `startup-final-{1,2,3}.log`; do not treat these Debug/warm-cache results as universal cold-start or Release timings.

Applied only small measured optimizations:

- Decoding dominated baseline startup (~6.02 s). Isolated stb's implementation in `stb_image.cpp` and optimized only that third-party translation unit in Debug. Engine code retains Debug checks/optimization settings. Visual Studio uses per-file `Optimization`/`BasicRuntimeChecks` metadata; non-MSVC uses per-file `-O2`. Other MSVC generators retain the correct unoptimized decoder path. No texture is skipped, resized, or lossy-transcoded: all 22 decodes remain. An intermediate decoder-only run measured 5.91 s total / 2.68 s decoding.
- Reused the same IBL sampling directions across texels, preserving sample count, resolution, accumulation order, and rendering quality. CPU energy/BRDF checks and GPU numerical checks still pass.
- Ignored redundant initial/restored SDL size events when drawable dimensions already match. Startup no longer creates and idles for a second identical swapchain/HDR/depth set. Genuine extent changes and Vulkan out-of-date/suboptimal events still recreate safely.
- Removed the unused default cube resource upload: six meshes, 37 buffer creations, and six upload-fence waits disappear. Its source asset and OBJ preview support remain. Unified instance ownership adds pose preparation for the six existing sphere instances, hence that row's slightly higher count.
- Canonical-path model reuse prevents repeat CPU decoding/GPU uploads. The two blackrats share one resource, while retaining independent animation/placement; the API check deliberately loads the same path twice and verifies reuse.

The remaining dominant Debug cost is decoding 22 images, including full-resolution 4K material maps. Large CPU geometry conversion and initial bounds also remain visible in per-asset/self rows. Pipeline creation is only ~3.6 ms total for all nine fixed variants, so a persistent Vulkan pipeline cache is **not justified by these measurements** and was not added. Upload-fence waits are ~0.10 s; introducing asynchronous/batched-upload machinery would add complexity without addressing the main measured cost. Existing upload fences and real resize/shutdown device-idle waits remain for safety. No new dependencies, asset streaming, threads, disk caches, renderer features, or gameplay systems were added.

## Phase 7 file inventory

Added:

- `engine/include/grimlock/input.hpp`: read-only frame snapshot interface (buttons, keys, motion, wheel, focus).
- `engine/include/grimlock/orbit_camera.hpp`, `engine/src/core/orbit_camera.cpp`: configuration and pure camera-control math, operating on the existing Camera.
- `engine/src/platform/input.hpp/.cpp`: SDL event-to-state adapter and drag-scoped relative-mode RAII.
- `tests/camera_navigation.cpp`, `tests/input.cpp`, `tests/navigation_runtime.cpp`: math, SDL state and Engine integration checks.

Modified:

- `engine/include/grimlock/engine.hpp`, `engine/src/core/engine.cpp`: input snapshot/access, one event pass, optional controller, active-camera synchronization and lifecycle cleanup.
- `engine/include/grimlock/scene.hpp`, `engine/src/scene/scene.cpp`: world-space camera writeback that retains object-local attachments and existing serialization.
- `game/src/game.cpp`: enable navigation after loading the scene/model preview; no input handling or camera math in Game.
- `CMakeLists.txt`, `engine/CMakeLists.txt`, `tests/CMakeLists.txt`: sources and optional focused tests; this README documents controls/settings/verification.

Removed: no files. No dependency versions, shader/rendering code, model data, JSON format, or original Camera view/projection math changed. No input mapping framework, keyboard camera controls, picking, gameplay, camera animation, or other next-phase functionality was introduced.

## Phase 6 file inventory

Added:

- `engine/include/grimlock/scene.hpp`: Scene/Object IDs, hierarchy/state, cameras/lights and serialization API; no SDL/Vulkan/JSON types.
- `engine/include/grimlock/transform.hpp`, `engine/src/core/transform.cpp`: existing Transform extracted for reuse by Scene and Engine.
- `engine/src/scene/scene.cpp`, `scene_serializer.cpp`: CPU scene behavior and private JSON reader/writer.
- `assets/scenes/demo.json`, `assets/models/pbr-comparison.glb`: authored demo and exported existing sphere sample.
- `tests/scene.cpp`, `tests/scene_runtime.cpp`: CPU hierarchy/schema/roundtrip and GPU lifecycle/public-API checks.

Modified:

- `CMakeLists.txt`, `engine/CMakeLists.txt`, `cmake/Dependencies.cmake`, `tests/CMakeLists.txt`: sources, private pinned JSON dependency, optional tests.
- `engine/include/grimlock/engine.hpp`, `engine/src/core/engine.cpp`: asset root, scene load/save/activation/unload, scene rendering, shared-resource collection and rollback.
- `engine/include/grimlock/assets/animation.hpp`, `engine/src/assets/animation.cpp`: read-only playback snapshot accessors/rest-pose tracking for saving; no new playback technique.
- `engine/include/grimlock/lighting.hpp`, `engine/src/renderer/lighting.cpp`: reuse existing CPU validation from scene loading.
- `engine/src/renderer/vulkan_renderer.hpp/.cpp`, `deformation.hpp/.cpp`: safely release GPU models/descriptor sets and reuse freed slots across scene changes; rendering passes/pipelines/shaders remain unchanged.
- `game/src/game.hpp/.cpp`, `main.cpp`: load the default/selected JSON scene; retain existing model-preview CLI, remove hardcoded demo construction/sphere generation.
- `README.md`, `assets/models/README.md`: format, API, ownership, asset notes and verification.

Removed: no production files, shaders, model formats, or user assets. Existing Transform declarations/implementation were moved rather than duplicated. Game's old demo-construction/sphere helpers were replaced by external data. The JSON library is the only new dependency; existing dependency versions and CMake presets remain unchanged. One-time sphere-export tooling and visual regression fixtures/captures are local artifacts under ignored `out/verification/`, not new production build tools.

## Earlier architecture-refactor file inventory (before Phase 6)

Added:

- `engine/CMakeLists.txt`, `game/CMakeLists.txt`.
- `engine/include/grimlock/engine.hpp`, `engine/src/core/engine.cpp` (public API and private lifecycle/resource coordination).
- `engine/src/core/startup_profile.hpp/.cpp` (Debug diagnostics).
- `engine/src/assets/stb_image.cpp` (isolated existing decoder implementation).
- `tests/CMakeLists.txt`, `tests/engine_api.cpp` (optional API consumer check).

Moved/split, with private includes updated throughout:

- `src/assets/` implementations and private headers -> `engine/src/assets/` (animation, environment, glTF import/materials/animation, image loading, model/mesh data, tangents).
- `src/assets/mesh_data.hpp` -> `engine/include/grimlock/assets/model_data.hpp`; `src/assets/animation.hpp` -> `engine/include/grimlock/assets/animation.hpp`.
- CPU image structs split from the old image loader header -> `engine/include/grimlock/assets/image_data.hpp`; decoder functions remain private in `engine/src/assets/image_loader.hpp`.
- `src/platform/` -> `engine/src/platform/` (`window`, `runtime_paths`, headers and sources).
- `src/renderer/` -> `engine/src/renderer/` (buffer, deformation, depth target, GPU materials/mesh, lighting uniforms, mesh/tone-map pipelines, shader module, texture, context/helpers/renderer, and camera implementation).
- Camera header -> `engine/include/grimlock/camera.hpp`; CPU lighting settings split -> `engine/include/grimlock/lighting.hpp`. Their public namespace is now `grimlock`; packed Vulkan-facing uniforms stay private.
- `src/main.cpp` -> `game/src/main.cpp`; `src/app/application.hpp/.cpp` replaced by `game/src/game.hpp/.cpp`, preserving demo policy while transferring lifecycle/events/loading/render coordination to Engine.

Modified beyond moves: root `CMakeLists.txt` (target composition), `cmake/Shaders.cmake` (root-binary shader staging), `shaders/mesh.slang` (unused Fresnel helper removal only), `assets/models/README.md`, and this README. Instrumentation touches asset import/decoding/preparation, window/context, pipeline/swapchain creation, buffers/images/mesh uploads, and synchronization waits. `environment.cpp` additionally reuses sample directions; `vulkan_renderer.cpp` avoids redundant extent recreation and reports successful presentation accurately. `mesh_pipeline.hpp/.cpp` drops an unused color-format accessor/member.

Removed/replaced: the old monolithic Application implementation and old source locations above; redundant startup cube work, unused pipeline getter/member, and unused shader helper. No working rendering feature or binary asset was deleted. Dependency versions/hashes, presets, project contract, and approved stack are unchanged. Local ignored regression projects were updated to the moved Engine sources/library; diagnostic captures/logs and temporary failure-test runtimes remain under `out/verification/`, not in production targets.
