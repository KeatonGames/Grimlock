# Grimlock

C++20, SDL3, and Vulkan 1.3+ renderer with glTF PBR, node/skeletal/morph animation, and punctual lights. The default scene preserves the character/cat/bunny placement, adds two independently posed blackrat instances sharing one GPU model, and retains six PBR spheres. Directional/point/spot lights and split-sum IBL illuminate the scene; HDR blending precedes tone mapping.

`Engine` is an internal static library; `Game` is the demo executable and links only its public API. Game loads `assets/scenes/demo.json`; the scene supplies assets, placement, hierarchy, cameras, lights, and clip settings. Engine owns platform initialization, loading, shared CPU/GPU resources, scene updates, rendering, and shutdown. The optional scene editor is isolated in `Editor`. No ECS, gameplay, hot reload, or new rendering/animation techniques are introduced.

## Build and run on Windows

Prerequisites: Visual Studio 2026 with Desktop development with C++, CMake on PATH, the Vulkan SDK with Slang (`slangc`), and a Vulkan driver supporting Dynamic Rendering, Synchronization2, and an sRGB swapchain format. The renderer negotiates the loader API up to 1.4 and explicitly checks the required 1.3 device features.

Verified toolchain: MSVC 19.50.35730.0, CMake 4.2.3, Vulkan SDK 1.4.363.0, Slang `2026.17-25-gc7954ebd1`, and an RTX 3060 Ti exposing Vulkan 1.4.341.

From PowerShell in the repository root:

```powershell
cmake --preset debug
cmake --build --preset debug --target Game --parallel 4
.\out\build\debug\Debug\Grimlock.exe
```

For Release:

```powershell
cmake --preset release
cmake --build --preset release --target Game --parallel 4
.\out\build\release\Release\Grimlock.exe
```

The CMake executable target is named `Game`, with output name `Grimlock` to preserve existing launch paths. Select `Game` in VS Code's CMake Tools target selector. The presets select x64. Visual Studio build presets select the configuration; `CMAKE_BUILD_TYPE` is not used. Initial dependency population needs network access. No package manager or privileged installation is required. The selected GLM release emits harmless upstream CMake deprecation warnings.

Model assets use Git LFS. After a fresh clone, materialize tracked `.glb` files with `git lfs pull` before building/staging assets. A small text file starting with `version https://git-lfs.github.com/spec/v1` is a pointer, not a usable GLB. The demo references the existing user-supplied `game/assets/models/assassin.glb`; its model data is preserved. No model data is silently substituted when assets are missing.

## Other CMake toolchains

The presets above select Visual Studio on Windows. On another platform, use an out-of-source build with the platform's C/C++20 compiler, Vulkan SDK/Slang, and SDL3 development prerequisites:

```sh
cmake -S . -B out/build/native -DCMAKE_BUILD_TYPE=Debug -DGRIMLOCK_BUILD_TESTS=ON
cmake --build out/build/native --parallel 4
ctest --test-dir out/build/native --output-on-failure
./out/build/native/Debug/Grimlock
```

The `Debug/` executable/runtime subdirectory is intentional, including with single-configuration generators. These commands are not a claim of Linux/macOS validation; a compatible Vulkan implementation and display are required.

## Minimal scene editor (Phase 9)

The optional `Editor` library uses the official [Dear ImGui v1.93.0](https://github.com/ocornut/imgui/releases/tag/v1.93.0) `v1.93.0-docking` tag and unmodified SDL3/Vulkan backends. `GRIMLOCK_BUILD_EDITOR` defaults to `ON`; the normal launch still runs Game without an ImGui context, panels or editor input handling. No extra application executable is introduced.

```powershell
cmake --preset debug -DGRIMLOCK_BUILD_EDITOR=ON -DGRIMLOCK_BUILD_TESTS=ON
cmake --build --preset debug --parallel 4
.\out\build\debug\Debug\Grimlock.exe --editor
# Or: Grimlock.exe --editor assets/scenes/my_scene.json
```

In editor mode:

1. **File > New Scene** starts an empty scene; **Open Scene** offers the runtime scene list and an editable JSON path. Invalid/missing files report an error without replacing the active scene.
2. **Scene Hierarchy** displays the actual Scene tree. Click a name or a model in the viewport to share the same stable-ID selection. Empty and hidden objects can remain selected for inspection; normal Game picking still ignores inactive/non-selectable objects. Selection reveals collapsed ancestors. Visibility checkboxes edit local enabled state; disabled ancestors still hide their children.
3. **Add Model** opens a compact asset picker; **Add to Scene** places the chosen model. The list scans the configured asset root's `models/` directory; **Refresh List** refreshes names only, not already loaded model contents. New model instances are fitted to roughly two world units, grounded and centered at the camera target. Source geometry/materials are not modified. New objects are selected automatically.
4. **Create Group** creates an empty Scene object at the root. Right-click an object for **Group Selected Object**, which wraps it in a new group under its existing parent without moving it. Drag model/group names onto groups to reparent them; drop onto **Scene Root** or the empty hierarchy area to unparent. Moves preserve world transforms and descendants, reject cycles/model targets, and retain selection. Double-click a name, or use **Rename** in its context menu, to edit it inline (Enter commits, Escape cancels). The Inspector can also rename groups. Groups and models have distinct row markers; arrows expand/collapse real children.
5. **Inspector** separates General and Transform properties, with aligned X/Y/Z-accented numerical controls for local position/XYZ rotation in degrees/scale. Drag values or Ctrl+click to enter numbers; viewport updates are immediate. Rotation uses a consistent GLM Euler/quaternion convention with a per-selection display cache; non-finite values are rejected and near-zero scales constrained. Negative scales remain supported. Groups show their direct-child count; models show a read-only asset path. Animated models also show the **Animation** controls described below.
6. **Duplicate** copies the selected object's local state, model/material reference, parent and authored animation configuration into an independent instance. Preview time/pause/stop are not copied; the new instance starts according to its Autoplay setting. It does not recursively clone children. **Delete Object/Group** asks for confirmation and explicitly reports how many descendants will also be deleted. Move children to Scene Root first to keep them. Shared CPU/GPU resources remain alive while other instances use them.
7. **Save Scene** writes the current JSON path; **Save Scene As** chooses a new `.json` filename. Group names, parent relationships and local transforms use the existing JSON schema. An asterisk marks edits, including camera navigation. New/Open/Exit and window-close requests ask **Save and Continue / Discard / Cancel** before losing unsaved changes. Normal animation playback and selection alone do not mark the document dirty.

World-preserving reparenting cannot always be expressed as translation/rotation/scale: rotated, non-uniformly scaled parents can require shear. Such moves report a clear error and leave hierarchy/transforms unchanged. Use uniform group scales or aligned rotations before retrying; the editor never silently approximates shear or changes the Scene transform format.

Paths entered in the file dialog are absolute or executable-relative, just like Engine's existing load/save APIs. Hover the scene name in the menu bar to see the current path. **Builds restage `game/assets/` beside the executable: use Save As to save persistent authored scenes to your source scene directory, or open an absolute source path before editing.** Saving a staged copy is not a source-file hot-reload mechanism. New models added to the source assets directory need normal asset staging before they appear in the running editor.

The default docked layout puts Hierarchy left and Inspector right, over the existing full-window 3D scene. The transparent center remains the viewport: RMB pan, MMB orbit, wheel zoom and left picking work there. UI mouse/keyboard capture suppresses the corresponding Engine input, including during Inspector drags and text entry. Panels can be rearranged within the same native window; multi-viewport support is disabled. Layout is session-only (`imgui.ini`/log output disabled). **View > Performance** optionally shows ImGui's rolling FPS/frame time and Scene object/enabled counts; no new draw-call tracker or profiler is installed.

Editor code lives only in `editor/`: its document commands call the real Engine/Scene APIs and never keep a parallel scene graph. Engine exposes a small opt-in native overlay interface for attachment, SDL event forwarding, capture, deferred close, swapchain changes and post-tone-map command recording. There are no ImGui dependencies in Engine or Scene, and Game's implementation is unchanged; only the executable launcher chooses editor mode. The backend renders inside the existing Dynamic Rendering color pass after tone mapping, under the existing frame fence. Device-idle detach/recreation preserves deterministic cleanup, and only format/image-count changes reinitialize the ImGui Vulkan backend. The slate dark theme uses muted blue accents, consistent padding/rounding and ImGui's embedded scalable font at 15 px, without OS font paths, extra assets or dependencies. The palette is linearized once for the existing sRGB attachment.

To exclude the editor and its dependency completely:

```powershell
cmake --preset debug -B out/build/game-only -DGRIMLOCK_BUILD_EDITOR=OFF
cmake --build out/build/game-only --config Debug --target Game --parallel 4
.\out\build\game-only\Debug\Grimlock.exe
```

The pinned ImGui archive/hash is isolated in `cmake/ImGui.cmake`; initial editor-enabled dependency population needs network access. No file-dialog library, custom UI renderer, gizmos, undo history, asset database or scene hot reload is added.

### Animation controls and public Engine API

Select an animated model to show **Animation Clip**, **Play/Pause/Resume**, **Stop**, **Loop**, **Autoplay**, and **Playback Speed** in the existing Inspector. Clip names come directly from imported data; unnamed clips receive a display-only numbered label. Static models and groups have no animation controls. The supplied `blackrat.glb` has 13 clips; `assassin.glb` has no clips and correctly shows no controls.

Changing the selected clip resets its time and updates its pose immediately: an already playing instance keeps playing, while a stopped/paused instance previews the new clip's first frame paused. Pause holds the exact current pose; Resume retains time; Play restarts a finished/stopped clip. Stop restores imported rest transforms, morph weights and skin palettes. Non-looping playback holds its endpoint and reports **Finished (holding pose)**; Stop returns to rest. Speed 1 is normal, 0 freezes advancement, and negative speed reverses; Play/restart with negative speed starts at the clip's end. Loop/speed edits affect the live instance without restarting it. Autoplay only controls the next scene initialization and does not start/stop the current preview.

Only clip/Loop/Speed/Autoplay edits mark the document dirty. Play/Pause/Resume/Stop and ordinary advancement never change authored settings. Saving during a preview does not save its time, paused state or pose. Each instance keeps independent playback and poses while sharing immutable model/clip data and GPU assets; no new evaluator or Vulkan work is introduced.

The public `grimlock::Scene` owned by Engine is the object-ID animation API for both Editor and Game (`auto& scene = *engine.scene();`, after scene activation/model preparation):

| API | Behavior |
| --- | --- |
| `availableAnimations(id)` | Read-only span of imported clips/names/start/end, empty for non-animated/unbound objects; valid until the object's model is replaced/removed |
| `playAnimation(id, clipIndexOrName, loop)` | Select/restart a runtime clip, without changing saved configuration; names must match exactly and uniquely |
| `pauseAnimation(id)`, `resumeAnimation(id)` | Freeze/resume the current playback time and pose |
| `stopAnimation(id)` | Stop and restore rest pose |
| `setAnimationSpeed(id, speed)` | Finite runtime speed only; does not change saved Speed |
| `animationSettings(id)` | Existing live snapshot: clip index, time, speed, loop, running/paused/rest flags; `playing` means running before pause filtering |
| `animationConfiguration(id)` | Authored clip, optional unique clip name, autoplay, loop and speed, independent of preview |
| `setAnimationConfiguration(id, config)` | Validate/save configuration; changed clip previews immediately, loop/speed update live, autoplay applies on reload |

For example, using actual imported names rather than hardcoded model-specific strings:

```cpp
auto& scene = *engine.scene();
const auto clips = scene.availableAnimations(objectId);
if (!clips.empty()) {
    scene.playAnimation(objectId, std::size_t{0}, true); // Or clips[0].name if unique/nonempty.
    scene.pauseAnimation(objectId);
    scene.resumeAnimation(objectId);
    scene.setAnimationSpeed(objectId, 0.75F); // Runtime only.
    scene.stopAnimation(objectId);

    auto config = scene.animationConfiguration(objectId);
    config.clip = 0;
    config.clipName.reset(); // Choose by index; a supplied name otherwise takes precedence.
    config.autoplay = true;
    config.speed = 1.0F;
    scene.setAnimationConfiguration(objectId, config);
}
```

Editor configuration edits go through `SceneDocument` to track dirty state; preview buttons call this public Scene API. Existing `Scene::setAnimation` / `Scene::animation` and manual `Engine::animation(InstanceId)` remain available for legacy initial-state input and lower-level playback/seek use. No state machines, blending, timeline or keyframe editing are added.

New scene saves write only these initial settings per animated instance:

```json
"animation": {"clip": "idle_A1", "autoplay": true, "loop": true, "speed": 0.75}
```

`clip` uses a unique nonempty imported name when possible, or a zero-based index for unnamed/duplicate-named clips. Missing/renamed/ambiguous names and out-of-range indices produce a warning at binding, retain the authored choice, and leave that instance at rest; the Inspector offers a new selection. A missing named selection resolves to `configuration.clip == availableAnimations(id).size()`. Invalid interactive API selections throw before changing state. Names survive clip reordering; index fallbacks cannot promise that. Scene format version remains 1: old numeric clip records and legacy `time`/`playing`/`paused`/`rest_pose` still load as initial playback state, but saving migrates them to configuration-only records. Explicit `autoplay` takes precedence over legacy playback flags/time. An absent `animation` starts at rest with autoplay off, loop on and speed 1; old records lacking `autoplay` preserve their previous playback defaults.

## Mouse navigation (Phase 7)

Game enables mouse navigation for both JSON scenes and standalone model previews:

| Input | Action |
| --- | --- |
| Right button + drag | Pan camera and target together, parallel to XZ |
| Middle button + drag | Orbit around the target; yaw/pitch, constant distance |
| Wheel up / down | Zoom in / out by changing distance, never field of view |
| Left-click | Select a scene model; empty space clears selection |
| Keyboard | Tracked only; no camera or gameplay actions |

There are no keyboard shortcuts, speed modifiers or FPS controls. Middle drag takes precedence if both navigation buttons are held. Drag entry/action changes discard that frame's motion so earlier pointer motion cannot cause a jump. Motion is displacement-based, not multiplied by frame time. Selection ignores clicks during right/middle navigation gestures.

Engine initializes the target from the active camera's existing `target`, deriving yaw/pitch/distance from `position - target`. Normal authored poses are preserved. Camera selection, scene replacement, and externally edited camera poses resynchronize the controller and cancel any ongoing drag. Navigation writes the selected scene camera (not a renderer-only override); attached cameras are converted back into object-local coordinates without losing their attachment. Existing camera JSON fields save/reload the navigated view; controller sensitivities/bounds are C++ settings, not new scene format fields.

Pan uses view-relative right/up directions projected onto the horizontal plane. The shift is `(-dx * rightXZ + dy * upXZ) * scale`, moving the world with the drag. Scale is `2 * distance * tan(verticalFov/2) / logicalWindowHeight * panSensitivity`; neither camera nor target changes height, orientation, or orbit distance. Scaling uses SDL window height, not Vulkan drawable pixels. SDL relative motion uses the configured sensitivity; no OS acceleration or SDL mouse-scale settings are changed.

Orbit reconstructs position as `target + distance * (cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))`, using fixed world up `(0,1,0)`. Horizontal drag subtracts yaw; downward drag raises pitch. Yaw wraps modulo 2*pi for continuous rotation, and pitch stays away from the pole. Zoom is `distance *= exp(-wheelY * zoomSensitivity)`, clamped in logarithmic space so large or fractional wheel inputs remain safe. Scrolling does not modify sensitivity or a movement-speed setting; pan still scales with current distance as specified.

The horizontal ground plane is Y=0. Pitch/radius limits also keep the camera above that plane when the target itself is below it (the demo target is y=-0.4): orbit raises its minimum pitch without changing radius; zoom raises its minimum radius without changing pitch. This is a navigation bound, not model collision/terrain detection. An initial/configured pose outside limits is constrained; unreachable targets/configurations are rejected.

### Configure or disable navigation

```cpp
engine.setCameraNavigationEnabled(); // Derives initial pose from the current camera.
auto settings = engine.cameraNavigationConfig();
settings.panSensitivity = 1.0F;
settings.rotationSensitivity = 0.005F;
settings.zoomSensitivity = 0.15F;
engine.configureCameraNavigation(settings); // Applies target/distance/angles to the current camera.
// engine.setCameraNavigationEnabled(false);
```

All math/configuration lives in Engine's Vulkan/SDL-free `OrbitCameraController`; Game only enables/configures it. Engine defaults to navigation disabled for other API consumers. `configureCameraNavigation` applies its complete pose explicitly; use a copy of the derived configuration when changing sensitivity only.

| `OrbitCameraConfig` field | Default / units |
| --- | --- |
| `target` | (0,0,0), replaced by camera target when enabling/resynchronizing |
| `distance` | 10 world units, derived from initial camera when enabling |
| `yaw`, `pitch` | 0 / 35 degrees expressed in **radians**; derived on enable |
| `panSensitivity` | 1, nonnegative multiplier |
| `rotationSensitivity` | 0.005 radians per mouse coordinate unit |
| `zoomSensitivity` | 0.15 exponential coefficient per wheel unit |
| `minDistance`, `maxDistance` | 0.5 / 100 world units, positive ordered bounds |
| `minPitch`, `maxPitch` | 5 / 85 degrees expressed in radians; minimum >=0, maximum <pi/2 |

### Input snapshot and cursor lifecycle

`engine.input()` returns a const `InputState` stable until the next update (explicit navigation lifecycle changes can cancel held navigation buttons). It provides pressed/held/released button/key states, accumulated relative mouse motion, fractional normalized wheel motion, window-coordinate position, first left-press coordinates and focus. Mouse buttons use `MouseButton`; keyboard indices are SDL physical scancodes [0,511], not text or mapped actions. Keyboard actions remain unmapped. SDL headers stay private to Engine; only adapter/integration tests include them explicitly.

Engine polls SDL once per frame, clears transient edges/deltas before polling, and passes the completed snapshot to the controller. Events for other windows are ignored by the input adapter. Focus loss/minimization releases held keys/buttons and discards pending motion/wheel. Resize cancels drags; focus regain requires a fresh button press. Scene/camera changes and controller disable also cancel navigation. No stale motion is reused.

During right/middle drag, SDL's per-window relative mode hides/constrains the cursor and supports motion at window edges; ending a drag restores the cursor to its starting window position. Focus loss releases relative mode without warping the cursor into an unfocused window. Disable/shutdown restore mouse mode via RAII; SDL failures are logged and cancel the gesture. See the [SDL3 relative-mode API](https://wiki.libsdl.org/SDL3/SDL_SetWindowRelativeMouseMode) and [wheel direction semantics](https://wiki.libsdl.org/SDL3/SDL_MouseWheelEvent). No permanent cursor capture, global event loop, or input mapping framework is introduced.

## Object selection (Phase 8)

Game enables picking for JSON scenes. Left-click selects the closest visible, selectable scene model; clicking empty space clears it. A held button does not repeatedly pick. RMB pan, MMB orbit, wheel zoom and navigation settings are unchanged, and selection survives navigation. The standalone legacy model-preview API has no scene object identifiers and is unchanged.

```cpp
engine.setObjectSelectionEnabled(); // Other Engine consumers default to disabled.
if (const auto id = engine.selectedObject()) {
    const auto& object = engine.scene()->object(*id);
    // Read object data here; no gameplay interaction is installed by Engine.
}
engine.clearSelection();
// engine.scene()->setSelectable(id, false); // Runtime policy, not a JSON field.
```

Scene stores one optional stable `ObjectId`, not an object pointer or GPU handle. Changing the selection replaces it; deletion (including ancestor deletion), disabling the selected hierarchy, removing/changing its model, making it non-selectable, unloading/replacing the scene or disabling selection safely invalidates/clears it. `Scene::selectObject`, `selectedObject` and `clearSelection` also expose the CPU-only state. Selection and selectable policy are intentionally not serialized; scene format version 1 is unchanged.

On a valid left-button press, Engine unprojects the logical cursor through the active camera's inverse view-projection at Vulkan depths 0 and 1. Logical coordinates are normalized by logical window size, while drawable pixel size determines the projection aspect. Camera projection already flips Y; no second Y flip is applied. Picking is restricted to the camera near/far interval and ignores unfocused, minimized/zero-sized, outside-window and navigation-drag clicks. Press coordinates are retained even if later motion occurs in the same event batch.

Each shared CPU model caches local mesh AABBs once. A world-unit ray is transformed into each instance/imported node's local space without renormalizing its direction, retaining comparable world-distance parameters under nonuniform or mirrored scale. AABB rejection precedes double-precision Moller-Trumbore triangle tests, respecting material back-face policy and overrides. Skinned/morphed positions and their exact bounds are evaluated only for clicks, using the current pose, morph-before-skin order and both sets of joint influences, including existing legacy bind-pose previews. One reusable per-resource scratch array avoids retaining duplicate meshes. Alpha-cutout hits sample the existing CPU base-color image at barycentric UV0/UV1, with texture transforms, sampler wrapping/filtering and material/instance alpha; fully transparent blended texels are also ignored. This is base-level CPU filtering, not an exact reproduction of GPU mip/derivative filtering or blended visibility. There is no GPU readback, physics system, BVH or per-frame geometry intersection work.

The selected object receives a soft cyan **inner silhouette rim plus a light surface tint**, not an external pixel-width outline. This deliberately simpler technique uses the existing mesh pass and one spare push-constant component; no additional image, pass, pipeline, descriptor or GPU geometry is allocated. Every primitive uses the same object flag, including animated children. Existing depth testing, material alpha discard/blending, skin/morph deformation, PBR lighting and tone mapping remain in force. Unselected shading and material data are unchanged; deselection immediately removes the transient effect. Occluded surfaces are not drawn through opaque foreground geometry. This is not a selection mask or a post-processing framework.

## Load an OBJ, GLB, or glTF

1. Put the model in `game/assets/models/`, for example `game/assets/models/statue.glb`. For `.gltf`, keep external buffers beside the file in their referenced relative locations.
2. Build normally; the assets directory is staged beside the executable even for asset-only changes.
3. Pass its executable-relative path:

```powershell
.\out\build\debug\Debug\Grimlock.exe assets/models/statue.glb
```

OBJ and `.gltf` arguments work the same way. Absolute paths also work. Relative model paths resolve beside the executable, independently of the working directory, including launches from VS Code. External buffers/images resolve relative to glTF files, including percent-escaped filenames. Build after adding assets to stage them beside the executable. Without an argument, Game loads the JSON demo below. A `.json` argument loads another scene; a model argument retains the standalone preview and starts its first available clip. `--help` prints usage. Cube/hierarchy/witch previews remain available; the unused default cube upload was removed, not the OBJ feature or asset.

Standalone model previews fit initial selected-scene bounds into a two-unit box using an instance matrix. JSON scenes use authored TRS directly; the demo's previous normalization is already folded into those transforms. Import evaluates positions once on the CPU for framing, including default morphs/skins (or the first clip's start pose); rendering deformation is exclusively on the GPU. Bounds are not recalculated to chase animated motion. Camera position, target/up orientation, field of view, and near/far planes remain independent. Perspective is right-handed, zero-to-one depth, with flipped projection Y and current swapchain aspect ratio.

## Data-driven scenes (Phase 6)

The default [demo scene](game/assets/scenes/demo.json) contains the same five character/cat/bunny/rat instances and six PBR spheres, plus one transform-only rat parent. The blackrats share one model; the six spheres share `models/pbr-comparison.glb`, an export of the previous procedural sample's geometry and six materials. It introduces no procedural-world feature. Existing user-supplied assets are unchanged.

```powershell
.\out\build\debug\Debug\Grimlock.exe assets/scenes/demo.json
```

Editing the **staged** JSON beside the executable changes the next launch without compiling C++. Edit `game/assets/scenes/demo.json` in the source tree and build `Game` to copy it to Debug/Release; the existing asset-staging target also runs when no C++ needs compilation. There is no live/hot reload.

### Scene API and ownership

`Scene`/`SceneObject` are Vulkan/SDL-free CPU types. A Scene owns its objects and independent playback/poses. Engine binds their model references to its existing loader/resource cache and supplies temporary draws to the renderer; the renderer never owns the Scene. An active Scene replaces legacy manual-instance drawing, camera and lighting configuration; unloading it restores that legacy API's behavior. Legacy `ModelId` resources remain pinned for their documented Engine lifetime.

```cpp
#include <grimlock/engine.hpp>
#include <grimlock/scene.hpp>

grimlock::Engine engine;
engine.setScene(engine.loadScene("assets/scenes/demo.json"));
auto& scene = *engine.scene();
const auto group = scene.createObject("New group");
const auto object = scene.createObject("Another bunny", group);
scene.setModel(object, "models/bunny2.glb"); // Relative to the asset root, not scene folder.
grimlock::Transform transform;
transform.position = {2, 0, -1};
transform.scale = {0.2F, 0.2F, 0.2F};
scene.setTransform(object, transform);
scene.setEnabled(object, true);
engine.saveScene("assets/scenes/saved.json"); // Saves definitions/current playback.
while (engine.update()) {}
engine.unloadScene();
```

`std::make_unique<Scene>("Empty")` creates an empty scene with a default camera/lighting; pass it to `setScene`. `Scene::load(nativePath)`/`save(nativePath)` work without a window/device. Engine's wrappers additionally resolve filenames beside the executable. Loading parses/validates a separate definition; `setScene` loads/binds all candidate models and validates initial frame/light limits **before** replacing the active scene. Missing assets, invalid clips/materials, and combined light overflow leave the previous scene intact. Failed candidate GPU/CPU resources are collected. Runtime edits to an active Scene bind newly referenced models on the next update; such edit errors throw to the caller, rather than silently reverting unrelated edits.

Object IDs are nonzero uint64 values, unique within one Scene, monotonically generated and never reused there. IDs and `next_id` survive saving, including deletion of the highest object ID. IDs from one Scene must not be used with another/replaced Scene. `object(id)` returns a const reference stable across insertions and other objects' edits, but invalid after that object is deleted or its Scene is destroyed. Use setters and ID lookup, not retained pointers across deletion/replacement. `animation(id)` is available after the object has been bound on activation/update. Calling the existing playback controls updates only that instance; Engine advances playback automatically.

An animation reference is invalidated by changing/removing that object's model, deleting the object, or replacing/unloading the Scene. Scene methods are single-threaded, like the existing Engine API. Do not copy playback instances back into objects or advance them a second time in Game.

Scene objects reference shared immutable CPU assets; Engine keeps one corresponding GPU model per canonical file. Deleting an object releases its pose/CPU reference immediately. Unused unpinned models are collected on the next update, replacement, or unload. Resources with another object/external CPU asset reference stay alive; disabled objects keep their resources resident. GPU collection uses one device-idle wait per release batch, frees per-mesh deformation sets before geometry, and reuses internal resource/descriptor slots. It does not add asynchronous streaming or an asset database. Engine shutdown still destroys scene references, GPU resources/device, then SDL.

### Hierarchies, transforms, cameras and lights

The two hierarchies have distinct responsibilities:

```text
Scene parent world * Scene object local TRS -> object world
object world * imported glTF node world    -> final mesh transform
```

Scene parenting never modifies model nodes, inverse binds, skins, morphs, or shared geometry. TRS is `T * R * S`, right-handed; rotations are quaternions stored **[x,y,z,w]**, normalized when needed, not Euler angles/degrees. Nonzero negative scales are supported; finite/invertible world matrices are required. Setters invalidate only affected subtrees. World matrices and inherited enabled state are cached and recalculated only when dirty; traversal is iterative, with reusable scratch storage. Animation still evaluates imported nodes independently inside each instance.

`setParent(child,parent)` prevents missing/self/circular parents and preserves the child's **local** TRS (world placement may change). Passing `std::nullopt` unparents. `removeObject(id)` deletes that entire scene subtree; deleted IDs throw on lookup. Disabling a parent hides its descendants and freezes their animation playback. Disabled objects are omitted from rendering and imported-light collection. An attached camera remains usable as the selected viewpoint regardless of the object's enabled flag.

Scenes contain named cameras and an `active_camera` selection. Camera position/target/up may be attached to an object; positions/targets transform as points and up as a vector. Directional, point and spot lights reuse `Lighting` and existing GPU buffers. Each can optionally attach to an object: positions/directions follow its world transform, while intensity/range/cones do not scale. Disabled attachments suppress lights. Deleting an attachment's object detaches surviving camera/light settings into world space, preserving their current placement (and suppressing formerly disabled lights).

Scene-defined and enabled imported glTF lights are combined, rather than replacing scene lights. Existing limits remain **one active directional, eight points and eight spots total**. To use an imported directional light, set the scene directional intensity to zero; multiple directional lights or overflow fail explicitly. IBL intensity/yaw/exposure remain independent. This changes only scene light collection, not Vulkan lighting techniques. The standalone preview retains its previous imported-light policy.

### JSON format version 1

Minimal example:

```json
{
  "version": 1,
  "name": "Two models",
  "objects": [
    {"id": "1", "name": "Group", "transform": {"position": [1, 0, 0]}},
    {
      "id": "2", "name": "Animated rat", "parent": "1",
      "model": "models/blackrat.glb", "enabled": true,
      "transform": {"position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [4, 4, 4]},
      "animation": {"clip": 1, "loop": true, "speed": 0.75}
    }
  ],
  "cameras": [{"id": "main", "position": [3.5, 2.2, 7.5], "target": [0, 0, 0]}],
  "active_camera": "main",
  "lighting": {
    "directional": {"direction": [-0.4, -0.8, -0.5], "color": [1, 1, 1], "intensity": 3},
    "points": [{"position": [2, 3, 2], "color": [1, 0.8, 0.6], "intensity": 20, "range": 8}],
    "spots": [], "environment_intensity": 1, "environment_rotation": 0, "exposure": 1
  }
}
```

IDs/references are decimal **strings**, not JSON floating-point numbers. Parent order in the file does not matter. Geometry/textures remain external model assets, never embedded in scene JSON.

| Field | Optional/default behavior |
| --- | --- |
| Root `version` | Required integer, exactly 1; newer versions rejected |
| `name`, `objects`, `next_id` | `Untitled`, empty array, next unused ID; saves write all three |
| Object `id` | Required unique decimal string in [1, 18446744073709551614] |
| `name`, `parent`, `model`, `enabled` | Empty name, no parent, transform-only object, true |
| `transform.position/rotation/scale` | [0,0,0] / [0,0,0,1] / [1,1,1] |
| `tint`, `material_override` | White [1,1,1,1], imported primitive materials; override selects existing model material index |
| `animation` | Omitted means rest pose/autoplay off, clip 0, speed 1, loop true. New records save clip name/index, autoplay, loop, speed; legacy records without autoplay retain their old startup defaults |
| `cameras`, `active_camera` | Default `main` Camera; supplied nonempty array selects first unless active ID specified |
| Camera properties | `id` required; position [3,2.2,4], target [0,0,0], up [0,1,0], `vertical_fov_degrees` 45, `near` 0.1, `far` 100 |
| Light attachment `object` | Optional object ID, otherwise world-space |
| `lighting.directional` | Existing default direction [-0.4,-0.8,-0.5], white, intensity 3 |
| `lighting.points/spots` | Empty arrays, at most eight each; existing light defaults for omitted fields |
| Point/spot properties | Position [0,0,0], color [1,1,1], intensity 20, range 0 (unbounded) |
| Spot extras | Direction [0,0,-1], `inner_cone` 0 / `outer_cone` pi/4 radians |
| IBL/exposure | `environment_intensity` 1, `environment_rotation` 0 radians, `exposure` 1 |

Camera/light `object` references can point to transform-only or model-bearing objects. Light colors/intensities/ranges must be nonnegative; spot angles satisfy `0 <= inner < outer <= pi/2`. Cameras use the existing non-degenerate view/perspective validation. Saving captures authored clip/autoplay/speed/loop configuration, never preview time, playing/paused/rest flags or mesh/skin pose arrays. No blending/state machine is added.

Scene JSON uses private, SHA-256-pinned nlohmann/json 3.12.0 for parsing/writing; fastgltf retains simdjson. Unknown/duplicate properties, malformed types/JSON, invalid references/cycles/TRS, unsupported versions, and invalid lights/cameras fail with file/field diagnostics. Limits: 16 MiB scene files, JSON depth 128, 10,000 objects/cameras, plus existing renderer frame/resource limits. Save builds the document before opening the destination and checks write errors; disk writes are synchronous, not a crash-proof transactional filesystem/database.

### Asset roots and adding models

`EngineConfig::assetRoot` defaults to executable-relative `assets`. Scene model paths are relative to **that root**, independent of the JSON file's folder or current working directory. Use portable `/` separators, e.g. `models/tree.glb`; absolute paths, drive paths and `..` traversal (including canonical paths escaping through links) are rejected. You can configure another native asset root explicitly. Standalone preview/scene-file arguments still support absolute paths.

To add a GLB, place it under `game/assets/models/`, add an object with a new unique ID and `"model": "models/your-model.glb"`, then author its local TRS in source units. Add `"parent": "some-id"` or `"animation": {"clip": 0}` if needed. Build `Game` once to stage the new asset/JSON. Referencing the same file from another object shares resources automatically; imported model hierarchy/materials remain unchanged. No runtime auto-normalization hides authored scene transforms.

## Model loading and hierarchy

The internal data flow is TinyObjLoader/fastgltf → immutable shared `ModelData` (geometry, materials, animations, skins, lights) → per-instance `ModelInstance`/`ModelPose` → shared GPU geometry/materials plus frame pose buffers → Vulkan rendering. Game calls `Engine::loadModel`, not the private importers or renderer. CPU representations/playback have no Vulkan or fastgltf types.

- The 124-byte `Vertex` owns position, normal, UV0/UV1, tangent XYZ/handedness, two joint/weight sets, and original morph vertex index. `MeshData` represents one triangle-list primitive with 32-bit indices/material index and optional target-major morph deltas. A glTF mesh with several primitives maps to several entries.
- `ModelNode` stores a name, optional parent index, child indices, primitive references, and a local matrix. Imported TRS becomes `T * R * S`; an explicit matrix stays unchanged. Transform-only nodes are retained. Repeated mesh references reuse the same primitive indices.
- `ModelData` owns geometry, materials, decoded images, samplers, nodes, scene roots, skins, animation samplers/channels, lights, initial bounds, and warnings. `modelNodeTransforms` validates the forest. Hierarchies, joint references, inverse binds, and imported geometry are preserved; skinning is no longer baked into vertices during import.
- The glTF default scene controls rendering and bounds. Without a default, the first scene is selected with a warning. Without scenes, all parentless nodes are selected with a warning. Nodes outside the selected scene remain in CPU data but are not drawn.
- Upload caches selected-node/primitive references and creates geometry/morph buffers once per primitive, never per instance/pose. Drawing uses `instanceTransform * pose.nodeWorld` and its inverse-transpose normal matrix. Instances share model data, GPU buffers, textures, and materials; only node poses, palettes, weights, and playback state differ. `ModelDraw::pose == nullptr` uses the renderer-owned rest pose. Changing hierarchy topology requires re-uploading; animation changes transforms/weights, not topology.

TinyObjLoader remains the OBJ importer. Each shape becomes a primitive and identity root node, using a white, two-sided dielectric fallback (metallic 0, roughness 0.7). Complete vertices are deduplicated, preserving normal and UV seams. Provided normals are normalized; missing/zero normals get geometric flat normals. Missing UVs become zero. Degenerate triangles are skipped with a warning, and invalid indices, missing files, and empty geometry report errors containing the file path.

TinyObjLoader triangulates faces. Export triangulated meshes for predictable results with complex/concave polygons; no additional triangulation library is included. MTL materials, textures, and OBJ vertex colors are not rendered. Missing-normal generation is flat shaded and does not synthesize smoothing groups.

fastgltf supports core glTF 2.0 `.glb`, external-buffer `.gltf`, and embedded/data-URI geometry buffers. Accessor utilities handle byte offsets, interleaved strides, sparse values, unsigned 8/16/32-bit indices, and normalized UV component conversion. Non-indexed triangles receive sequential indices. Missing/zero normals receive flat geometric normals without losing UV seams; missing UVs are zero. Storage ranges, attribute types/counts, index ranges, finite values, hierarchy cycles, and scene references are checked before rendering.

Geometry limits remain explicit: triangle lists, UV sets 0/1, PNG/JPEG material images, and up to eight influences (`JOINTS_0/1`, `WEIGHTS_0/1`). Additional influences and unsupported compression/quantization are rejected with diagnostics. Imported cameras and unused attributes (including vertex colors) are reported but not rendered. Selected scenes require triangle geometry. Model/skinned-mesh transforms must remain finite and invertible; singular animated scales report an error. Culling follows double-sided state and model/node determinant; per-triangle winding inversions from negative joint scales are not separately classified. See the extension table below for material compatibility.

## Textures and materials

`image_loader.cpp` uses a pinned stb_image revision to decode PNG/JPEG into tightly packed top-left-origin RGBA8. RGB JPEGs receive alpha 255; PNG alpha is preserved. `decodeImage` accepts encoded memory, and `loadImage` reads native filesystem paths. glTF conversion handles embedded GLB buffer views, data URIs, and external files. Errors identify the model and image. Dimensions are checked before decoding and again against the GPU limit.

`MaterialData` holds base-color RGBA, metallic, roughness, emissive RGB, normal scale, occlusion strength, five optional texture references, OPAQUE/MASK/BLEND, cutoff, and double-sided state. Each texture independently selects UV0/UV1 and offset/rotation/scale. Every primitive references its own material; unassigned primitives use glTF defaults: white base color, metallic/roughness 1, black emissive, normal scale/AO strength 1, OPAQUE, cutoff 0.5, single-sided.

All referenced PBR images are decoded. Base color and emissive use sRGB images with hardware conversion to linear RGB; metallic-roughness, normals, and occlusion use linear UNORM images. Roughness is the packed green channel, metallic the blue channel, and occlusion the red channel. Factors multiply samples; AO is `mix(1, sample.r, strength)` and affects indirect lighting only. Normal scale multiplies tangent-space XY before normalization. Missing textures use neutral white samples; missing normal textures are explicitly disabled. Explicit broken image references fail rather than silently substituting white.

Imported glTF tangents retain handedness. If a normal map needs a compatible frame (including UV1 or transformed UVs), pinned MikkTSpace generates it in the undeformed mesh. Original vertices/indices remain unchanged where frames agree; only normal/tangent seams split vertices, retaining each original morph index and skin weights. Missing normal-map UVs or degenerate UV triangles warn and use geometric normals. Other required missing UVs remain errors. Skin and model normal transforms use inverse transpose; tangents use linear transforms and determinant signs. The fragment shader orthogonalizes TBN and handles double-sided faces. A separate clearcoat normal uses a derivative frame for its own transformed UV set.

`Texture` owns its device-local Vulkan image, memory, and view. It supports RGBA8 sRGB/UNORM material images, RGBA16F precomputed 2D/cube mip chains, and an RGBA16F scene attachment. Uploads reuse the mesh staging-buffer/fenced-submit path. Synchronization2 transitions cover copies, each material mip blit, and final shader-read layout. Linear blit capability is checked before generating material mip chains; unsupported generation falls back to one level with a warning. Required sampled/filtering/color-attachment/blending capabilities are checked; unsupported formats fail clearly.

`GpuMaterials` uploads once per model image/color-space pair, even when several textures/materials reference it, and reuses matching samplers. An image used as both color and data requires separate Vulkan formats/uploads. glTF nearest/linear magnification and minification, all four mip filters, repeat/mirrored-repeat/clamp-to-edge, and non-mipmap LOD clamping are honored. A renderer-owned white fallback is shared by untextured materials.

Descriptor organization:

| Set | Binding | Resource | Lifetime |
| --- | --- | --- | --- |
| 0 | 0 | 144-byte camera view/projection/world-position UBO | Renderer/frame |
| 0 | 1 | 832-byte bounded lighting UBO | Renderer/frame |
| 0 | 2/3/4 | Irradiance cube / specular cube / BRDF LUT | Renderer/environment |
| 1 | 0 | Combined base-color image sampler | Model material |
| 1 | 1 | 416-byte material UBO (factors, flags, ten UV transforms) | Model material |
| 1 | 2/3/4/5 | Metallic-roughness / normal / AO / emissive samplers | Model material |
| 1 | 6/7/8/9/10 | Specular weight/color / coat factor/roughness/normal | Model material |
| 2 | 0/1/2 | Draw-record / joint-matrix / morph-weight SSBOs | Renderer/frame |
| 2 | 3 | Immutable primitive morph-delta SSBO | Shared mesh |

The separate tone-map pipeline uses its own set 0, binding 0 for the HDR scene sampler, plus a single float exposure push constant.

Each model owns a descriptor pool and immutable sets, one per material. Instances bind the same sets; there are no per-draw allocations/updates or per-frame image uploads. Resources are released only after the device is idle; pools are destroyed before their samplers, images, and buffers. The existing 128-byte push constants carry model/normal matrices and a neutral-by-default instance tint.

Eight fixed graphics-pipeline variants share one layout: opaque/masked versus blended, single/double-sided, and normal/mirrored winding. Pipelines are not created per material/object. OPAQUE ignores alpha; MASK discards below cutoff before writing depth; BLEND uses straight-alpha blending, depth testing without depth writes, and back-to-front view-space primitive-center sorting across all instances. Intersecting/concave transparent surfaces can still require finer sorting; order-independent transparency is outside this phase. Double-sided back-face normals are reversed for lighting.

`mesh.slang` evaluates Cook-Torrance with GGX, separable exact Smith geometry, and Schlick Fresnel. Dielectric F0 defaults to 0.04, or derives from IOR and specular RGB/weight; specular alpha scales both F0 and F90. Metallic F0 uses base color. Diffuse energy uses the maximum RGB Fresnel component. Roughness is squared for GGX alpha and clamped to 0.045. Clearcoat adds a separate GGX/IBL lobe and attenuates the base/emission using the extension's documented Fresnel layering model. Unlit bypasses all lighting; emissive strength multiplies emission. Lighting remains linear HDR, AO affects indirect only, and tone mapping follows blending.

Meshes and straight-alpha blending render into RGBA16F, then a full-screen triangle applies exposure and the Narkowicz filmic curve fit (not a complete ACES color-management pipeline). Only the sRGB swapchain performs final encoding; no extra gamma correction is applied. BGRA8/RGBA8 sRGB presentation formats are required. HDR scene values are bounded to the finite half-float range.

### Configure lights

`grimlock::Lighting` is independent of meshes/materials. Configure it through `Engine::lighting()`; Game's example is in `game/src/game.cpp`:

```cpp
auto& lighting = engine.lighting();
lighting.directional.direction = {-0.4F, -0.8F, -0.5F}; // Direction rays travel; normalized on upload.
lighting.directional.color = {1, 1, 1}; // Linear RGB.
lighting.directional.intensity = 3;
lighting.points[0] = {{-2, 2, 2}, 28, {1, 0.8F, 0.65F}, 8};
lighting.pointCount = 1; // Maximum 8.
lighting.environmentIntensity = 1;
lighting.environmentRotation = 0; // Yaw radians.
lighting.exposure = 1; // Positive linear exposure multiplier.
// Engine::update() uploads lighting and renders.
```

Point intensities use inverse-square, candela-like scene units. Range 0 means no cutoff; positive ranges apply a smooth finite-distance cutoff. Nonnegative colors/intensities, finite values, a nonzero directional vector, positive exposure, and the eight-light bound are checked. Camera and light buffers update after the single frame fence; there are no per-frame descriptor updates or GPU allocations.

`Lighting::spots`/`spotCount` additionally support eight spotlights with position, direction, intensity/color, range, and inner/outer cone angles in radians. Spot attenuation squares the interpolated cosine-cone weight and applies inverse-square/range attenuation. Engine collects active `KHR_lights_punctual` node references using instance and animated node transforms, never scaling intensity/range/cones. Standalone previews retain their original replacement of configured direct lights by imported lights; active Scenes combine scene-defined and imported lights as described above. IBL remains independent. Limits are one active directional, eight points, eight spots total; overflow fails explicitly.

### Configure environment lighting

Without input, a small generated studio-gradient cubemap with bright lobes keeps metals visible. It lights objects only; there is no sky renderer. To use a cubemap, provide six equally sized square faces in a folder: `px`, `nx`, `py`, `ny`, `pz`, `nz` (+X, -X, +Y, -Y, +Z, -Z), each with `.hdr`, `.png`, `.jpg`, or `.jpeg`. Radiance HDR values remain linear; PNG/JPEG are converted from sRGB explicitly. Faces use top-left origin and Vulkan cube orientation: direction at face coordinates `(u,v)` is respectively `(1,-v,-u)`, `(-1,-v,u)`, `(u,1,v)`, `(u,-1,-v)`, `(u,-v,1)`, `(-u,-v,-1)`, where u/v span -1 to +1.

Place the folder under `game/assets/environments/studio/` before building, or pass an absolute folder:

```powershell
.\out\build\debug\Debug\Grimlock.exe --environment assets/environments/studio
.\out\build\debug\Debug\Grimlock.exe assets/models/dogcat.glb --environment C:/Environments/studio
```

Paths are executable-relative unless absolute. Missing/invalid faces fail with a diagnostic. Input faces are limited to 4096 square; the resulting radiance/irradiance must fit positive finite RGBA16F (maximum 65504). Equirectangular panoramas, KTX/DDS containers, and glTF environment extensions are not supported.

CPU preprocessing runs once at renderer creation, not per frame or resize: cosine-weighted irradiance (8 square per face), GGX prefilter (64 square through 1 square, seven mip levels), and a 64-square BRDF integration LUT, with 128 Hammersley samples per integration. Sample directions are reused across texels without changing the samples or accumulation order. The GPU uses ordinary cube/2D sampling and roughness-dependent specular LOD. These intentionally modest settings keep startup/resources bounded; very small bright sources can alias, CPU filtering clamps within source faces, and no multiscattering compensation is implemented. Constants live in `engine/src/assets/environment.cpp`. No disk cache, hot reload, or additional IBL library is introduced.

### Supplied GLB inspection and static-pose limitation

| Asset | Meshes/primitives | Nodes | Materials | Images/textures | Skins | Animations |
| --- | --- | --- | --- | --- | --- | --- |
| `assassin.glb` | 27/27 | 216 | 4 | 12/12 | 27 | 0 |
| `dogcat.glb` | 1/1 | 35 | 1 | 0/0 | 1 | 0 |
| `blackrat.glb` | 2/3 | 238 | 3 | 6/10 | 1 (235 joints) | 13 |
| `bunny.glb` | 20/22 | 20 | 11 | 2/2 | 0 | 0 |
| `bunny2.glb` | 22/22 | 45 | 11 | 4/4 | 0 | 0 |

`assassin.glb` uses UV0/imported tangents, four double-sided OPAQUE materials, four embedded JPEG base-color images (three 4096², one 2048²), four JPEG metallic-roughness maps, and four PNG normal maps. All twelve images are decoded/uploaded and shaded, with repeat/linear/trilinear sampling. Metallic/roughness factors default to 1, so the packed maps control their values. `dogcat.glb` has no textures; its double-sided OPAQUE material uses linear RGBA `(0.5, 0.5, 0.5, 1)`, metallic 0, and roughness about 0.6. Neither asset contains AO/emissive textures, UV transforms, or material extensions; synthetic checks cover those channels.

`assassin.glb` and `dogcat.glb` have identity local transforms on every referenced joint despite non-identity inverse binds, and no clips. Their declared rest skins collapse geometry. For this narrowly detected all-identity/coincident-joint case only, the importer warns and marks **undeformed bind-pose preview in joint-root space**; the GPU palette implements the preview without altering imported geometry or node transforms. Re-export complete joint transforms to reproduce an intended skeletal pose. This legacy compatibility path is never applied to animated blackrat.

### Blackrat failure and fix

Reproduced against the source file before changes: `Unsupported glTF extension: KHR_materials_clearcoat`. The old loader enabled only texture transforms and rejected every other `extensionsUsed` entry, even optional material extensions. Blackrat uses clearcoat on its eyes and specular/IOR on masked fur (specular texture uses UV1, IOR approximately 1.45). All six images are embedded PNGs; all primitives are uncompressed triangle lists with valid resident buffer/accessor ranges. It does **not** require KTX2/Basis, mesh compression, quantization, sparse decoding changes, or morph targets. No decoder/compression dependency was added.

The loader now enables/converts these extensions and the shader renders them rather than dropping them. All referenced images are decoded and cached, including fur specular alpha. Skin and animation data now survive import. All 13 clips retain their source order: `_Static Pose`, three idle clips, four run-related clips, and five walk-related clips. Clip 0 intentionally has a static pose; loading a single model starts clip 0 as requested. In the default demonstration, the second instance shares the same GPU model but plays clip 1 (`idle_A1`) at speed 0.75, so actual motion is visible.

A separate executable-relative-path check also found that launching an old build before staging the newly added file yields a missing-file failure. The loader now says explicitly to rebuild/stage new assets; use an absolute source path when diagnosing independently of staging. Parser failures include the numeric fastgltf error and available `extensionsUsed`/`extensionsRequired` names. Image failures identify material, texture index, and image index; geometry errors identify primitive/accessor/buffer-view resources.

### Extension compatibility

| Extension | Status |
| --- | --- |
| `KHR_texture_transform` | Rendered, independently per supported texture; UV0/UV1 |
| `KHR_materials_unlit` | Rendered; base RGBA/alpha state without lighting |
| `KHR_materials_emissive_strength` | Rendered in HDR |
| `KHR_materials_ior` | Rendered in dielectric Fresnel (including IOR 0 convention) |
| `KHR_materials_specular` | Factor, color, alpha-weight texture, sRGB color texture; energy-conserving diffuse |
| `KHR_materials_clearcoat` | Factor/roughness/normal and textures; separate direct/IBL lobe and Fresnel layering |
| `KHR_lights_punctual` | Directional/point/spot; node/parent animation supported |
| `KHR_materials_transmission`, `KHR_materials_volume`, `KHR_materials_sheen` | Typed factors, colors, texture references/transforms and decoded images preserved. Optional use warns and renders only the core surface; no fake opacity/diffuse approximation. Required use fails explicitly because the advanced layer is not rendered. |
| `KHR_texture_basisu` / KTX2 | Unsupported; needs libktx or Basis Universal transcoder. None of the supplied assets require it. |
| `KHR_draco_mesh_compression`, `EXT_meshopt_compression`, `KHR_mesh_quantization` | Unsupported; explicit diagnostics. No supplied asset needs them. |
| All other extensions | Unsupported and rejected explicitly, including optional uses; no claim of universal glTF compatibility. |

Material color textures (base/emissive/specular RGB) use sRGB; all factor/normal/specular-alpha textures use linear formats. Advanced extension rendering is not implied merely because its metadata is retained. See the official [glTF specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html), [clearcoat layering](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_clearcoat), [specular/IOR](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_specular), and [punctual lights](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_lights_punctual). The implementation uses the pinned fastgltf 0.9.1 [accessor API](https://fastgltf.readthedocs.io/v0.9.x/tools.html), not an unrequested dependency upgrade.

## Animation and GPU deformation

`AnimationData` stores immutable samplers (times/output values/interpolation) and target channels separately from playback. `SkinData` stores joint node indices, inverse binds, and optional skeleton root. Nodes retain rest TRS or explicit matrices, skin/light references, and default morph weights. `ModelInstance` shares `const ModelData` but owns its selected clip/time/speed, playback flags, and `ModelPose`. It has no Vulkan calls. Engine advances instances using a steady-clock frame delta and supplies their poses to the renderer:

```cpp
const auto model = engine.loadModel("assets/models/blackrat.glb");
const auto first = engine.createInstance(model);
const auto second = engine.createInstance(model);
if (engine.modelInfo(model).animationNames.size() >= 2) {
    engine.animation(first).play(0, true);
    engine.animation(second).play(1, false);
    engine.animation(second).setSpeed(0.75F);
}
// Engine::update() advances both; do not update them again in Game.
```

`pause()`/`resume()` freeze/resume time, `seek(seconds)` evaluates a clamped clip time, `stop()` restores the rest pose, and `stop(false)` retains the current pose. `play` selects/restarts a clip. Looping wraps within actual clip start/end; non-looping playback holds its endpoint and stops. Speed may be zero/negative; negative playback can seek to the end first. Channels clamp outside their own key ranges. STEP holds the preceding value; LINEAR interpolates vectors/weights and uses shortest-path quaternion slerp. CUBICSPLINE evaluates Hermite in/value/out triples with key-interval-scaled tangents, then normalizes quaternion results without sign-flipping cubic keys. Untargeted properties retain their rest values. Animated matrix nodes, duplicate channels, invalid times/counts/quaternions, and invalid skin references fail with diagnostics.

Parent-first evaluation computes `world = parentWorld * local` for all nodes, including joints outside the selected scene. For each skinned mesh node, palette entries are `inverse(meshWorld) * jointWorld * inverseBind`; the final draw matrix is `instanceWorld * meshWorld`. Skinned and rigid attachments therefore inherit the correct hierarchy, and differently posed instances need no duplicate geometry. Eight weights are normalized together; joint indices are checked even in zero-weight lanes, which shaders may fetch.

Morph targets preserve position/normal/tangent deltas and default mesh/node weights. CUBICSPLINE/STEP/LINEAR weight channels use flattened glTF scalar ordering. The vertex shader applies weighted morphs **before** skinning. Morph indices survive flat-normal/Mikk seam splits. Inverse-transpose skin normals and linearly transformed tangents handle non-uniform skin scales; singular blended skin matrices retain geometric normals instead of producing NaNs. Supplied assets have no morph targets, so a synthetic glTF/GPU check exercises this combined path.

`Deformation` owns a single fence-protected coherent SSBO arena: draw records, joint matrices, and morph weights. Immutable per-mesh set 2 descriptors additionally bind the device-local morph buffer. Direct indexed draws use Vulkan's instance index to select a record, without extra shader-draw-parameter features or push-constant growth. Matrices/weights upload only after the existing frame fence; there are no per-frame GPU allocations or descriptor updates. Current explicit limits: 4096 uploaded primitive sets, 4096 primitive draws/frame, 65536 palette entries/frame, and 65536 weights/frame. Palettes/weights are currently appended per primitive draw; these are small-scene bounds, not a streaming allocator. Device storage-buffer limits and buffer sizes are checked; all descriptors use aligned offset zero. Resizing preserves these resources.

No animation blending/state machine/timeline is included. The optional scene Inspector controls existing playback only. Bounds remain initial framing bounds and transparent sorting still uses primitive centers rather than recomputed deformed bounds. Stop/rest does not reconstruct missing source joint transforms. The only CPU deformation is initial position-only bounds calculation, not rendering.

### Place multiple instances (legacy manual API)

Load once, then create independent instances. This minimal complete public-API example uses no SDL, Vulkan, private headers, or renderer types:

```cpp
#include <grimlock/engine.hpp>
#include <grimlock/assets/animation.hpp> // Only needed for playback controls.

int main() {
    grimlock::Engine engine;
    const auto model = engine.loadModel("assets/models/hierarchy.glb");
    const auto first = engine.createInstance(model);
    const auto second = engine.createInstance(model);
    grimlock::Transform left;
    left.position = {-2, 0, 0};
    grimlock::Transform right;
    right.position = {2, 0, 0};
    right.scale = {0.8F, 0.8F, 0.8F};
    engine.setTransform(first, left);
    engine.setTransform(second, right);
    engine.setTint(first, {0.2F, 0.6F, 0.9F, 1});
    engine.camera().position = {3.5F, 2.2F, 7.5F};
    while (engine.update()) {}
} // Engine shuts down via RAII, including exception unwinding.
```

`Transform` composes position, quaternion rotation, and scale as `T * R * S`; `setTransform` also accepts a matrix. Use white instance tint to preserve imported colors; setting a tint multiplies the material. `game/src/game.cpp` demonstrates bounds normalization and original demo placement. Reusing a `ModelId` shares geometry, images, samplers, and material descriptor sets. The original CC0 [`hierarchy.glb`](game/assets/models/hierarchy.glb) remains available to exercise multiple primitives, nested nodes, repeated references, TRS/matrices, and default-scene selection. See [asset notes and license](game/assets/models/README.md). User-supplied model files are unchanged.

The default PBR comparison references one exported sphere mesh and six materials in `pbr-comparison.glb`, with six JSON objects using `material_override`. The upper row is dielectric (metallic 0), lower row metallic (1); columns increase roughness from 0.15 through 0.5 to 0.9. Shared geometry makes the material differences independent of imported assets. `Engine::addModel(ModelData)` remains available for existing CPU-generated geometry.

### Public API and lifetime

| API | Responsibility |
| --- | --- |
| `Engine(EngineConfig)`, destructor | One window/device, optional environment folder, deterministic initialization/shutdown |
| `loadModel(path)`, `addModel(ModelData)` | Load supported files or existing procedural CPU geometry; return a reusable `ModelId` |
| `modelInfo(model)` | Initial bounds, primitive/material counts, clip names |
| `createInstance(model)` | Independent transform, tint, material override, playback and pose |
| `setTransform`, `transform`, `setTint`, `setMaterialOverride` | Change an instance, not the shared resource |
| `animation(instance)` | Existing CPU playback controls: play/pause/resume/stop/seek/speed |
| `camera()`, `lighting()` | Mutable Vulkan-free camera and lighting settings |
| `update()` | Poll events, advance animation/imported lights, render/present; false when closing |

Minimization defers rendering but does not end the update loop. First update uses zero delta so asset-loading time does not advance clips. Legacy ModelId/InstanceId values are Engine-local, must only be passed to their originating Engine, and remain valid until its destruction; default/out-of-range IDs throw. Their resources stay pinned until shutdown. Scene ObjectIds/references follow the shorter lifetimes described above; unused scene resources can be released. `modelInfo().materials` includes an internal fallback material where applicable. Legacy camera/lighting methods configure manual drawing only; active scenes supply their own settings. Bounds normalization and clip selection remain authored/Game policy, not hidden Engine behavior.

## Structure and ownership

```text
.
├── CMakeLists.txt         Project composition and optional tests
├── CMakePresets.json      Windows Debug/Release workflows
├── cmake/                Pinned dependencies (including optional ImGui) and shaders
├── editor/               Optional editor library: public launcher, UI and document commands
├── engine/
│   ├── CMakeLists.txt     Engine static library and shader target
│   ├── include/grimlock/ Public API (including CPU asset types in assets/)
│   ├── shaders/          mesh.slang, tone_map.slang
│   └── src/
│       ├── animation/    CPU animation playback and poses
│       ├── assets/       OBJ/glTF import, image decoding, tangents, IBL preparation
│       ├── camera/       Camera matrices and orbit navigation math
│       ├── core/         Engine lifecycle, transforms, startup profiling
│       ├── input/        Private SDL input adapter
│       ├── platform/     SDL window and executable-relative runtime paths
│       ├── renderer/     Vulkan frames, pipelines, GPU resources and deformation
│       └── scene/        CPU hierarchy, state and JSON serialization
├── game/
│   ├── CMakeLists.txt     Game executable and asset staging
│   ├── src/              main.cpp, game.hpp, game.cpp
│   └── assets/
│       ├── models/       OBJ/GLB samples and asset license notes
│       └── scenes/       demo.json (portable model references)
├── tests/                Optional API, scene, camera and input checks
├── docs/
│   └── history.md        Earlier validation reports, measurements and inventories
├── README.md
├── AGENTS.md
├── .gitattributes
└── .gitignore
```

Generated builds, fetched third-party sources (`_deps/`), caches and local verification artifacts stay under ignored `out/`; they are not source modules. Core dependency versions/hashes remain centralized in `cmake/Dependencies.cmake`, with optional editor dependencies in `cmake/ImGui.cmake`, without a duplicate vendored tree. No empty directories are reserved for future systems. Public header paths remain stable; private implementation files use snake_case names.

Source and runtime ownership are distinct: `game/assets/` is staged as `assets/` beside the executable, while `engine/shaders/` is compiled into runtime `shaders/`. Existing JSON model references, CLI arguments and public Engine includes are unchanged.

Dependency direction is `Game -> Engine -> platform/graphics/import dependencies`; Engine never includes Game. Only public headers and GLM propagate as compile requirements. SDL/Vulkan/loader includes remain private; static-library link requirements still resolve through CMake. One `Engine` pimpl hides implementation types, with no abstract factory, resource singleton, service locator, or new framework.

Engine owns immutable shared CPU model resources and corresponding GPU model IDs. Canonical executable-relative/asset-root paths reuse one resource. Scene owns stable objects and independent playback; imported model hierarchies remain separate from the small scene-object forest. CPU models contain no Vulkan handles, and file loaders never call Vulkan. The thread-local startup profiler only routes diagnostics; it owns no engine resources.

Destruction runs instances/draw references and CPU resources first, then renderer/device resources, then the SDL window. `VulkanContext` owns the instance, surface, device, and queues. `VulkanRenderer` owns swapchain/frame resources, environment textures, lighting buffers, and uploaded models. `MeshPipeline` and `ToneMapPipeline` own pipelines/layouts; `ShaderModule` RAII destroys temporary modules after pipeline creation. `Buffer`, `GpuMesh`, `Texture`, and `DepthTarget` release resources before the context/device. Existing constructor failure cleanup and safe device-idle shutdown are preserved. Owning Engine/GPU wrappers cannot be accidentally copied.

Static vertex/index data is uploaded once using coherent host-visible staging buffers, copied to device-local buffers, and synchronized with transfer-to-vertex-input barriers and an upload fence. There are no per-frame geometry uploads or GPU allocations. Native Vulkan allocation is sufficient for this small resource set; VMA and a custom suballocator are intentionally not added.

The existing one-frame-in-flight fence and per-swapchain-image presentation semaphores are preserved. Persistently mapped coherent camera/light UBOs update only after waiting for that frame fence. Per-draw push constants contain the model matrix, CPU-computed inverse-transpose normal matrix (with determinant sign in spare padding), and configurable tint, fitting the guaranteed 128-byte limit.

Dynamic Rendering binds HDR color and a supported depth attachment (D32 float, otherwise D16). Depth clears to 1; indexed draws use LESS testing, with writes for opaque/masked materials only. A second color-only pass samples HDR into the swapchain. Synchronization2 covers acquisition, uploads, color/depth access, HDR-write to fragment-read, and presentation. Shared depth/HDR are safe with one frame in flight. HDR/depth targets and command buffers are recreated with the swapchain; GPU meshes/materials/IBL remain resident. The HDR sampler descriptor updates only during recreation, after idle. Dynamic viewport/scissor preserve pipelines on size-only resizes; only the presentation pipeline depends on swapchain format. Minimized/zero-extent windows defer rendering.

The functioning Vulkan responsibility split is retained. The full-screen tone-map triangle remains necessary, not obsolete Hello Triangle code. Only the generic overlay callback bridge is new; editor widgets stay outside the renderer. No shadows, deferred lighting, SSR, ray tracing, physics, audio, gameplay or advanced transmission post-processing is introduced.

## Shaders and runtime files

`cmake/Shaders.cmake` finds SDK/PATH Slang and compiles `engine/shaders/mesh.slang` and `engine/shaders/tone_map.slang` into SPIR-V 1.5 modules with `vertMain`/`fragMain`, using column-major matrices. Edits automatically rebuild/stage outputs. CPU size/offset assertions enforce interfaces. The full-screen vertex shader uses Slang's `SV_VulkanVertexID`, avoiding an unnecessary `shaderDrawParameters` requirement.

Each configuration stages the same runtime layout (Windows executable/DLL names shown):

```
Grimlock.exe
SDL3.dll
shaders/mesh.spv
shaders/tone_map.spv
assets/models/cube.obj
assets/models/hierarchy.glb
assets/models/assassin.glb
assets/models/dogcat.glb
assets/models/blackrat.glb
assets/models/bunny.glb
assets/models/bunny2.glb
assets/models/pbr-comparison.glb
assets/models/witch.obj
assets/scenes/demo.json
```

Keep shaders, assets, and the SDL shared library alongside the executable when moving a build. The default demo uses the supplied `assassin.glb`; it is not generated or copied from another model. Paths use SDL's executable base directory, with UTF-8 path handling, not hardcoded absolute paths.

## Dependencies

C++20 and the standard library, Vulkan, SDL3, GLM, TinyObjLoader, fastgltf (with simdjson), stb_image, MikkTSpace, nlohmann/json, Slang, and CMake are the active stack. JSON is header-only/private to Engine serialization; no JSON types leak into the API. No graphics engine or IBL library is added.

Core downloaded libraries are centralized in `cmake/Dependencies.cmake` with explicit archive versions and SHA-256 validation:

| Library | Pinned tag | SHA-256 |
| --- | --- | --- |
| SDL3 | release-3.4.16 | `c2ee715e42ec520c4d11fd8d249ef1d2b2baf4ad31148b72b3b000276c0b3633` |
| GLM | 1.0.1 | `9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c` |
| TinyObjLoader | v1.0.7 | `b9d08b675ba54b9cb00ffc99eaba7616d0f7e6f6b8947a7e118474e97d942129` |
| fastgltf | v0.9.1 | `d8705a496deb2831775baeef5009be36d24bf3d10f8d29ab016e977d0119c872` |
| simdjson | v4.6.11 | `61d948fc24f0d793829ad658058e7597d064988a89b4607ea02e401a82df98ff` |
| stb (stb_image 2.30) | `2c980bb59875b0d32144a71867fbdebb2f77cd20` | `9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515` |
| MikkTSpace | `3e895b49d05ea07e4c2133156cfa94369e19e409` | `aeba65ddee85a679133d510d71d00b108f603752f90b15b9ecf7777033f6e351` |
| nlohmann/json | v3.12.0 (`json.tar.xz`) | `42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa` |
| Dear ImGui (optional, `cmake/ImGui.cmake`) | v1.93.0-docking | `746c265540bd3aa109570f29492ef4d8f5b421dd9fd0049c35ca35f21496f1a5` |

[TinyObjLoader v1.0.7](https://github.com/tinyobjloader/tinyobjloader/tree/v1.0.7) is the latest stable tag verified upstream; v2 tags are release candidates. Its legacy CMake 2.8 configuration cannot run unchanged under CMake 4, so FetchContent retrieves the release and this project builds its documented `tiny_obj_loader.cc` library source directly. Upstream examples, install rules, and unrelated tools are excluded. SDL retains only needed subsystems, and GLM is header-only. Vulkan and Slang come from the system SDK.

[fastgltf v0.9.1](https://github.com/spnda/fastgltf/releases/tag/v0.9.1) is the stable release verified for this integration. The loader uses its release API: `GltfDataBuffer::FromPath`, `Parser::loadGltf` with `LoadExternalBuffers`, `validate`, GLM [accessor utilities](https://fastgltf.readthedocs.io/latest/tools.html), and `getLocalTransformMatrix`. Its required simdjson is pinned separately to the upstream-required version; no implicit downloads or system parser version override the pin. Dependency tests/examples/docs/install targets are disabled.

[stb_image](https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h) is compiled in one CPU-only translation unit, with PNG/JPEG for materials and Radiance HDR additionally for environments. No global flip or gamma settings are changed. [MikkTSpace](https://github.com/mmikk/MikkTSpace/tree/3e895b49d05ea07e4c2133156cfa94369e19e409) is built directly from its C source under its upstream zlib-style license; enabling C in CMake is solely for this source. Both libraries lack release tags, so full revisions/digests are pinned; upstream license files remain in FetchContent source directories.

## Verification

The committed checks use no testing framework. Scene/camera CPU checks need no window/device; Engine/scene/camera runtime checks need a Vulkan display, and model checks need the supplied assets. Public API consumers do not include private Engine headers; the SDL input-adapter check explicitly includes its private header. Scene tests link JSON only to generate/check fixtures:

```powershell
cmake --preset debug -DGRIMLOCK_BUILD_TESTS=ON
cmake --build --preset debug --parallel 4
ctest --test-dir out/build/debug -C Debug --output-on-failure
```

The asset-dependent checks require materialized Git LFS models. The default demo uses `game/assets/models/assassin.glb`. No user models are substituted or recreated.

Earlier phase-specific results and startup measurements are preserved in [Development history](docs/history.md).

### Repository cleanup verification

Windows Debug and Release configure/build successfully, and all six CTest checks pass in each configuration with Vulkan core and synchronization validation enabled. Both mesh and tone-map SPIR-V outputs validate for Vulkan 1.3. An isolated Debug runtime containing only current shaders/source assets passes a stationary model preview and an animated scene derived from the existing demo's rat objects, including wide/tall/repeated resize, minimize/restore, unrelated working directory and clean shutdown, with no validation errors/warnings. Blackrat's default clip 0 is intentionally static; the animated check uses its existing idle clip 1.

The Release Game also passes the existing `witch.obj` preview with resize/minimize and validation-clean shutdown. Every moved model, scene and asset note was checked against its pre-move SHA-256. Camera, animation, model-data and shader moves preserve implementation content; only the SDL adapter's private include path changes. Public headers, Game logic, dependency versions and scene JSON are unchanged. Other operating systems were not tested.

### Phase 8 verification and file inventory

Debug and Release configure/build successfully; all eight CTest checks pass with Vulkan core/synchronization validation enabled. Mesh SPIR-V validates for Vulkan 1.3. CPU picking checks load and intersect every supplied GLB/OBJ, including `assassin.glb`, and cover Vulkan/DPI unprojection, resize/aspect, clipping, exact triangle rejection, mirrored/nonuniform scales, imported hierarchy, independent shared instances, current animation, morph-before-skinning, the second influence set and stable selection invalidation. Runtime SDL events exercise first-press/held/empty clicks, nearest overlapping models, runtime selectable/enabled flags, navigation conflicts, pan/orbit/zoom, resizing, serialization without new fields, deletion/unload, and actual clicks on the multi-mesh assassin and both shared rat instances. Existing animation and camera tests remain unchanged and pass.

Fixed-pose GPU captures verify that selecting the assassin or one rat changes only that object's rendered pixels, not its shared twin or the other models. Highlight rendering survives wide/tall/repeated resize and minimize/restore with no core/synchronization validation errors/warnings. Captures and the small local comparison harness stay under ignored `out/verification/`; no Windows-only capture code enters Engine/Game. DPI math is unit-tested at multiple logical/drawable ratios; a physical mixed-DPI display migration and other operating systems were not tested.

Additional checks cover transparent cutouts, UV1/texture-transform alpha, material overrides and instance alpha. The CPU import/picking smoke check enumerates all supported model files in `game/assets/models/`, including user additions, rather than maintaining a duplicate asset list. Pixel comparisons measured 7,776 changed pixels exclusively on the assassin and 1,264 exclusively on the selected rat. Bright bunny selection also remains visible. The Release Game passes the default animated demo with repeated resize/minimize/restore, and separate `witch.obj`, `hierarchy.glb` and `bunny.glb` previews; no validation warnings/errors were reported. Existing asset-specific missing-rest-joint warnings remain importer diagnostics, not Vulkan validation errors.

Added:

- `engine/src/scene/picking.hpp/.cpp`: private ray generation, shared static bounds, click-time posed geometry and nearest triangle intersection.
- `tests/picking.cpp`, `tests/selection_runtime.cpp`: CPU and Engine/SDL picking regressions.

Modified:

- `engine/include/grimlock/scene.hpp`, `engine/src/scene/scene.cpp`: CPU-only single selection and runtime selectable policy.
- `engine/include/grimlock/engine.hpp`, `engine/src/core/engine.cpp`: minimal selection API, per-resource bounds cache, input-to-scene picking and draw flags.
- `engine/include/grimlock/input.hpp`, `engine/src/input/input.cpp`, `tests/input.cpp`: first-press coordinates and cancellation/hold coverage.
- `engine/src/renderer/vulkan_renderer.hpp/.cpp`, `mesh_pipeline.hpp`, `engine/shaders/mesh.slang`: transient whole-object flag and depth-tested soft rim/tint in the existing pass.
- `game/src/game.cpp`: enable scene selection; no picking math or Vulkan resources in Game.
- `CMakeLists.txt`, `engine/CMakeLists.txt`, `tests/CMakeLists.txt`: source/test registration; no new dependencies.
- This README, `docs/history.md`, `game/assets/models/README.md`: selection documentation and current assassin asset name. The demo JSON already referenced `models/assassin.glb`; no model file was renamed/recreated, and no scene schema or material/animation data changed.

### Phase 9 verification and file inventory

The official pinned ImGui docking release configures through CMake and compiles with its SDL3/Vulkan backends unmodified. Debug and Release editor-enabled builds pass all nine CTest checks with core and synchronization validation enabled. `Editor.Runtime` exercises empty/hidden selection, hierarchy/local-TRS roundtrips, quaternion/Euler consistency, invalid-value/world-transform rollback, adding/duplicating/deleting shared model instances, failed-model/open rollback, file saving, actual Inspector text entry and keyboard capture, blocked UI mouse gestures, viewport pan, camera dirty tracking, unsaved-close cancellation and context teardown/reattachment. Group-management checks also cover nested JSON roundtrips, world-preserving moves with mirrored and aligned non-uniform scales, descendant transforms, cycle/invalid-target/shear rejection, real hierarchy drag/drop to groups and root, inline renaming, and delete-confirmation cancellation/acceptance. Existing eight Engine/scene/camera/picking tests also pass. No model binaries, materials, shader code or JSON schema were changed by the editor work.

Debug and Release editor-mode graphical smoke checks show the existing animated demo beneath docked panels, then resize wide/tall repeatedly, minimize/restore and close normally with no Vulkan validation errors/warnings. An independent `GRIMLOCK_BUILD_EDITOR=OFF` build compiles/runs the normal animated Game scene without an Editor/ImGui target and rejects `--editor` explicitly. Local captures, logs, downloaded archives and test outputs remain under ignored `out/`. Other operating systems and physical mixed-DPI monitor migration were not tested.

Added:

- `cmake/ImGui.cmake`: optional dependency pin/hash and official backend build.
- `editor/CMakeLists.txt`, `editor/include/grimlock/editor.hpp`: optional static library and launcher.
- `editor/src/editor.hpp/.cpp`: ImGui context/backends, docking, panels, input capture and file/unsaved/error dialogs.
- `editor/src/scene_document.hpp/.cpp`: commands over the real Scene, paths, dirty tracking and model enumeration; no parallel object graph.
- `engine/include/grimlock/render_overlay.hpp`: opt-in native overlay lifecycle/target/input contract, without an ImGui dependency.
- `tests/editor_runtime.cpp`: document, native-backend interaction and lifecycle regressions.

Modified for this phase:

- `CMakeLists.txt`, `game/CMakeLists.txt`, `game/src/main.cpp`: optional library/CLI mode; no second application executable.
- `engine/include/grimlock/engine.hpp`, `engine/src/core/engine.cpp`: overlay attachment, capture/close routing, asset-root access and explicit scene validation.
- `engine/include/grimlock/scene.hpp`, `engine/src/scene/scene.cpp`: read-only child enumeration and opt-in inactive/empty selection; default Game picking policy unchanged.
- `engine/src/input/input.hpp/.cpp`: suppress captured mouse/keyboard snapshots and release viewport mouse mode safely.
- `engine/src/renderer/vulkan_context.hpp`, `vulkan_renderer.hpp/.cpp`: native context accessor, generic post-tone-map callback and device-idle overlay lifecycle/target notifications; no editor widgets or ImGui code.
- `tests/CMakeLists.txt`: conditional editor regression registration; this README documents usage, structure and verification.

### Animation-controls changes and verification

Debug and Release builds pass all nine CTest checks with Vulkan core/synchronization validation enabled and no validation warnings/errors. Editor smoke runs in both configurations preserve animated rendering through wide/tall/repeated resizing, minimization/restoration and clean shutdown. The selected animated Inspector was visually checked at the unchanged 1280x720 layout. An Editor-disabled Game build also succeeds. Tests/captures/logs stay under ignored `out/`; other operating systems were not exercised. No new external setup is required.

Modified files for the animation-controls task (no new dependencies, targets, source files, shaders or model assets):

- `engine/include/grimlock/assets/animation.hpp`: change loop mode without restarting playback.
- `engine/include/grimlock/scene.hpp`, `engine/src/scene/scene.cpp`: public per-object playback/configuration API, immutable clip queries, name resolution and missing-clip rest fallback.
- `engine/src/scene/scene_serializer.cpp`: configuration-only saves and backwards-compatible legacy/name-based loading.
- `editor/src/editor.hpp/.cpp`: Animation Inspector controls over the public Scene API.
- `editor/src/scene_document.hpp/.cpp`: dirty tracking for authored animation edits and duplication of configuration rather than preview state.
- `tests/scene.cpp`, `tests/scene_runtime.cpp`, `tests/editor_runtime.cpp`: legacy/new JSON, name/index controls, missing clips, actual UI selection/playback/checkboxes, pause/rest poses, loop/speed, autoplay and shared-instance independence.
- `README.md`: API, configuration, behavior and compatibility documentation.
