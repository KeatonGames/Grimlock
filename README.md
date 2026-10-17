# Grimlock

C++20, SDL3, and Vulkan 1.3+ renderer with glTF PBR, node/skeletal/morph animation, and punctual lights. The default scene preserves the character/cat/bunny placement, adds two independently posed blackrat instances sharing one GPU model, and retains six PBR spheres. Directional/point/spot lights and split-sum IBL illuminate the scene; HDR blending precedes tone mapping.

`Engine` is an internal static library; `Game` is the demo executable and links only its public API. Game loads `assets/scenes/demo.json`; the scene supplies assets, placement, hierarchy, cameras, lights, and clip settings. Engine owns platform initialization, loading, shared CPU/GPU resources, scene updates, rendering, and shutdown. No ECS, gameplay, editor, hot reload, or new rendering/animation techniques are introduced.

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

## Load an OBJ, GLB, or glTF

1. Put the model in `assets/models/`, for example `assets/models/statue.glb`. For `.gltf`, keep external buffers beside the file in their referenced relative locations.
2. Build normally; the assets directory is staged beside the executable even for asset-only changes.
3. Pass its executable-relative path:

```powershell
.\out\build\debug\Debug\Grimlock.exe assets/models/statue.glb
```

OBJ and `.gltf` arguments work the same way. Absolute paths also work. Relative model paths resolve beside the executable, independently of the working directory, including launches from VS Code. External buffers/images resolve relative to glTF files, including percent-escaped filenames. Build after adding assets to stage them beside the executable. Without an argument, Game loads the JSON demo below. A `.json` argument loads another scene; a model argument retains the standalone preview and starts its first available clip. `--help` prints usage. Cube/hierarchy/witch previews remain available; the unused default cube upload was removed, not the OBJ feature or asset.

Standalone model previews fit initial selected-scene bounds into a two-unit box using an instance matrix. JSON scenes use authored TRS directly; the demo's previous normalization is already folded into those transforms. Import evaluates positions once on the CPU for framing, including default morphs/skins (or the first clip's start pose); rendering deformation is exclusively on the GPU. Bounds are not recalculated to chase animated motion. Camera position, target/up orientation, field of view, and near/far planes remain independent. Perspective is right-handed, zero-to-one depth, with flipped projection Y and current swapchain aspect ratio.

## Data-driven scenes (Phase 6)

The default [demo scene](assets/scenes/demo.json) contains the same five character/cat/bunny/rat instances and six PBR spheres, plus one transform-only rat parent. The blackrats share one model; the six spheres share `models/pbr-comparison.glb`, an export of the previous procedural sample's geometry and six materials. It introduces no procedural-world feature. Existing user-supplied assets are unchanged.

```powershell
.\out\build\debug\Debug\Grimlock.exe assets/scenes/demo.json
```

Editing the **staged** JSON beside the executable changes the next launch without compiling C++. Edit `assets/scenes/demo.json` in the source tree and build `Game` to copy it to Debug/Release; the existing asset-staging target also runs when no C++ needs compilation. There is no live/hot reload.

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
| `animation` | Omitted means rest pose; if present, clip 0, time 0 (clamped to clip), speed 1, loop/playing true, paused/rest_pose false |
| `cameras`, `active_camera` | Default `main` Camera; supplied nonempty array selects first unless active ID specified |
| Camera properties | `id` required; position [3,2.2,4], target [0,0,0], up [0,1,0], `vertical_fov_degrees` 45, `near` 0.1, `far` 100 |
| Light attachment `object` | Optional object ID, otherwise world-space |
| `lighting.directional` | Existing default direction [-0.4,-0.8,-0.5], white, intensity 3 |
| `lighting.points/spots` | Empty arrays, at most eight each; existing light defaults for omitted fields |
| Point/spot properties | Position [0,0,0], color [1,1,1], intensity 20, range 0 (unbounded) |
| Spot extras | Direction [0,0,-1], `inner_cone` 0 / `outer_cone` pi/4 radians |
| IBL/exposure | `environment_intensity` 1, `environment_rotation` 0 radians, `exposure` 1 |

Camera/light `object` references can point to transform-only or model-bearing objects. Light colors/intensities/ranges must be nonnegative; spot angles satisfy `0 <= inner < outer <= pi/2`. Cameras use the existing non-degenerate view/perspective validation. Saving captures clip/time/speed/loop/playing/paused/rest-pose settings, not mesh/skin pose arrays. No blending/state machine is added.

Scene JSON uses private, SHA-256-pinned nlohmann/json 3.12.0 for parsing/writing; fastgltf retains simdjson. Unknown/duplicate properties, malformed types/JSON, invalid references/cycles/TRS, unsupported versions, and invalid lights/cameras fail with file/field diagnostics. Limits: 16 MiB scene files, JSON depth 128, 10,000 objects/cameras, plus existing renderer frame/resource limits. Save builds the document before opening the destination and checks write errors; disk writes are synchronous, not a crash-proof transactional filesystem/database.

### Asset roots and adding models

`EngineConfig::assetRoot` defaults to executable-relative `assets`. Scene model paths are relative to **that root**, independent of the JSON file's folder or current working directory. Use portable `/` separators, e.g. `models/tree.glb`; absolute paths, drive paths and `..` traversal (including canonical paths escaping through links) are rejected. You can configure another native asset root explicitly. Standalone preview/scene-file arguments still support absolute paths.

To add a GLB, place it under `assets/models/`, add an object with a new unique ID and `"model": "models/your-model.glb"`, then author its local TRS in source units. Add `"parent": "some-id"` or `"animation": {"clip": 0}` if needed. Build `Game` once to stage the new asset/JSON. Referencing the same file from another object shares resources automatically; imported model hierarchy/materials remain unchanged. No runtime auto-normalization hides authored scene transforms.

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

Place the folder under `assets/environments/studio/` before building, or pass an absolute folder:

```powershell
.\out\build\debug\Debug\Grimlock.exe --environment assets/environments/studio
.\out\build\debug\Debug\Grimlock.exe assets/models/dogcat.glb --environment C:/Environments/studio
```

Paths are executable-relative unless absolute. Missing/invalid faces fail with a diagnostic. Input faces are limited to 4096 square; the resulting radiance/irradiance must fit positive finite RGBA16F (maximum 65504). Equirectangular panoramas, KTX/DDS containers, and glTF environment extensions are not supported.

CPU preprocessing runs once at renderer creation, not per frame or resize: cosine-weighted irradiance (8 square per face), GGX prefilter (64 square through 1 square, seven mip levels), and a 64-square BRDF integration LUT, with 128 Hammersley samples per integration. Sample directions are reused across texels without changing the samples or accumulation order. The GPU uses ordinary cube/2D sampling and roughness-dependent specular LOD. These intentionally modest settings keep startup/resources bounded; very small bright sources can alias, CPU filtering clamps within source faces, and no multiscattering compensation is implemented. Constants live in `engine/src/assets/environment.cpp`. No disk cache, hot reload, or additional IBL library is introduced.

### Supplied GLB inspection and static-pose limitation

| Asset | Meshes/primitives | Nodes | Materials | Images/textures | Skins | Animations |
| --- | --- | --- | --- | --- | --- | --- |
| `base.glb` | 27/27 | 216 | 4 | 12/12 | 27 | 0 |
| `dogcat.glb` | 1/1 | 35 | 1 | 0/0 | 1 | 0 |
| `blackrat.glb` | 2/3 | 238 | 3 | 6/10 | 1 (235 joints) | 13 |
| `bunny.glb` | 20/22 | 20 | 11 | 2/2 | 0 | 0 |
| `bunny2.glb` | 22/22 | 45 | 11 | 4/4 | 0 | 0 |

`base.glb` uses UV0/imported tangents, four double-sided OPAQUE materials, four embedded JPEG base-color images (three 4096², one 2048²), four JPEG metallic-roughness maps, and four PNG normal maps. All twelve images are decoded/uploaded and shaded, with repeat/linear/trilinear sampling. Metallic/roughness factors default to 1, so the packed maps control their values. `dogcat.glb` has no textures; its double-sided OPAQUE material uses linear RGBA `(0.5, 0.5, 0.5, 1)`, metallic 0, and roughness about 0.6. Neither asset contains AO/emissive textures, UV transforms, or material extensions; synthetic checks cover those channels.

`base.glb` and `dogcat.glb` have identity local transforms on every referenced joint despite non-identity inverse binds, and no clips. Their declared rest skins collapse geometry. For this narrowly detected all-identity/coincident-joint case only, the importer warns and marks **undeformed bind-pose preview in joint-root space**; the GPU palette implements the preview without altering imported geometry or node transforms. Re-export complete joint transforms to reproduce an intended skeletal pose. This legacy compatibility path is never applied to animated blackrat.

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

No animation blending/state machine/editor is included. Bounds remain initial framing bounds and transparent sorting still uses primitive centers rather than recomputed deformed bounds. Stop/rest does not reconstruct missing source joint transforms. The only CPU deformation is initial position-only bounds calculation, not rendering.

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

`Transform` composes position, quaternion rotation, and scale as `T * R * S`; `setTransform` also accepts a matrix. Use white instance tint to preserve imported colors; setting a tint multiplies the material. `game/src/game.cpp` demonstrates bounds normalization and original demo placement. Reusing a `ModelId` shares geometry, images, samplers, and material descriptor sets. The original CC0 [`hierarchy.glb`](assets/models/hierarchy.glb) remains available to exercise multiple primitives, nested nodes, repeated references, TRS/matrices, and default-scene selection. See [asset notes and license](assets/models/README.md). User-supplied model files are unchanged.

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

```
engine/
  CMakeLists.txt          Engine static library; target-scoped dependencies
  include/grimlock/      Public Engine, Scene/Object, Transform, Camera, Lighting, CPU assets
  src/
    core/                Engine implementation/lifecycle and Debug startup profiling
    scene/               CPU scene hierarchy, playback ownership, JSON serialization
    assets/              Private OBJ/glTF import, image decode, tangents and IBL preparation
    platform/            Private SDL window and executable-relative paths
    renderer/            Private Vulkan context, frames, pipelines, resources and deformation
game/
  CMakeLists.txt          Game executable; links Engine's public interface
  src/                   CLI, initial scene selection/model preview, application loop
tests/                   Optional API, CPU scene/serialization, GPU lifecycle checks
shaders/      Slang PBR mesh and full-screen tone-map entry points
assets/
  models/     External OBJ/GLB files and sample asset license notes
  scenes/     Versioned JSON demo; portable model references
data/         Reserved for future authored game data; no schema or loader
cmake/        Pinned dependencies and shader compilation
```

Dependency direction is `Game -> Engine -> platform/graphics/import dependencies`; Engine never includes Game. Only public headers and GLM propagate as compile requirements. SDL/Vulkan/loader includes remain private; static-library link requirements still resolve through CMake. One `Engine` pimpl hides implementation types, with no abstract factory, resource singleton, service locator, or new framework.

Engine owns immutable shared CPU model resources and corresponding GPU model IDs. Canonical executable-relative/asset-root paths reuse one resource. Scene owns stable objects and independent playback; imported model hierarchies remain separate from the small scene-object forest. CPU models contain no Vulkan handles, and file loaders never call Vulkan. The thread-local startup profiler only routes diagnostics; it owns no engine resources.

Destruction runs instances/draw references and CPU resources first, then renderer/device resources, then the SDL window. `VulkanContext` owns the instance, surface, device, and queues. `VulkanRenderer` owns swapchain/frame resources, environment textures, lighting buffers, and uploaded models. `MeshPipeline` and `ToneMapPipeline` own pipelines/layouts; `ShaderModule` RAII destroys temporary modules after pipeline creation. `Buffer`, `GpuMesh`, `Texture`, and `DepthTarget` release resources before the context/device. Existing constructor failure cleanup and safe device-idle shutdown are preserved. Owning Engine/GPU wrappers cannot be accidentally copied.

Static vertex/index data is uploaded once using coherent host-visible staging buffers, copied to device-local buffers, and synchronized with transfer-to-vertex-input barriers and an upload fence. There are no per-frame geometry uploads or GPU allocations. Native Vulkan allocation is sufficient for this small resource set; VMA and a custom suballocator are intentionally not added.

The existing one-frame-in-flight fence and per-swapchain-image presentation semaphores are preserved. Persistently mapped coherent camera/light UBOs update only after waiting for that frame fence. Per-draw push constants contain the model matrix, CPU-computed inverse-transpose normal matrix (with determinant sign in spare padding), and configurable tint, fitting the guaranteed 128-byte limit.

Dynamic Rendering binds HDR color and a supported depth attachment (D32 float, otherwise D16). Depth clears to 1; indexed draws use LESS testing, with writes for opaque/masked materials only. A second color-only pass samples HDR into the swapchain. Synchronization2 covers acquisition, uploads, color/depth access, HDR-write to fragment-read, and presentation. Shared depth/HDR are safe with one frame in flight. HDR/depth targets and command buffers are recreated with the swapchain; GPU meshes/materials/IBL remain resident. The HDR sampler descriptor updates only during recreation, after idle. Dynamic viewport/scissor preserve pipelines on size-only resizes; only the presentation pipeline depends on swapchain format. Minimized/zero-extent windows defer rendering.

The functioning Vulkan responsibility split is retained instead of introducing another abstraction layer. The full-screen tone-map triangle remains necessary, not obsolete Hello Triangle code. No shadows, deferred lighting, SSR, ray tracing, physics, audio, UI, gameplay, or advanced transmission post-processing is introduced.

## Shaders and runtime files

`cmake/Shaders.cmake` finds SDK/PATH Slang and compiles `mesh.slang` and `tonemap.slang` into SPIR-V 1.5 modules with `vertMain`/`fragMain`, using column-major matrices. Edits automatically rebuild/stage outputs. CPU size/offset assertions enforce interfaces. The full-screen vertex shader uses Slang's `SV_VulkanVertexID`, avoiding an unnecessary `shaderDrawParameters` requirement.

Each configuration contains:

```
Grimlock.exe
SDL3.dll
shaders/mesh.spv
shaders/tonemap.spv
assets/models/cube.obj
assets/models/hierarchy.glb
assets/models/base.glb
assets/models/dogcat.glb
assets/models/blackrat.glb
assets/models/bunny2.glb
assets/models/pbr-comparison.glb
assets/scenes/demo.json
```

Keep shaders, assets, and the SDL DLL alongside the executable when moving a build. Paths use SDL's executable base directory, with UTF-8 path handling, not hardcoded absolute paths.

## Dependencies

C++20 and the standard library, Vulkan, SDL3, GLM, TinyObjLoader, fastgltf (with simdjson), stb_image, MikkTSpace, nlohmann/json, Slang, and CMake are the active stack. JSON is header-only/private to Engine serialization; no JSON types leak into the API. No graphics engine or IBL library is added.

All downloaded libraries are centralized in `cmake/Dependencies.cmake` with explicit archive versions and SHA-256 validation:

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

[TinyObjLoader v1.0.7](https://github.com/tinyobjloader/tinyobjloader/tree/v1.0.7) is the latest stable tag verified upstream; v2 tags are release candidates. Its legacy CMake 2.8 configuration cannot run unchanged under CMake 4, so FetchContent retrieves the release and this project builds its documented `tiny_obj_loader.cc` library source directly. Upstream examples, install rules, and unrelated tools are excluded. SDL retains only needed subsystems, and GLM is header-only. Vulkan and Slang come from the system SDK.

[fastgltf v0.9.1](https://github.com/spnda/fastgltf/releases/tag/v0.9.1) is the stable release verified for this integration. The loader uses its release API: `GltfDataBuffer::FromPath`, `Parser::loadGltf` with `LoadExternalBuffers`, `validate`, GLM [accessor utilities](https://fastgltf.readthedocs.io/latest/tools.html), and `getLocalTransformMatrix`. Its required simdjson is pinned separately to the upstream-required version; no implicit downloads or system parser version override the pin. Dependency tests/examples/docs/install targets are disabled.

[stb_image](https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h) is compiled in one CPU-only translation unit, with PNG/JPEG for materials and Radiance HDR additionally for environments. No global flip or gamma settings are changed. [MikkTSpace](https://github.com/mmikk/MikkTSpace/tree/3e895b49d05ea07e4c2133156cfa94369e19e409) is built directly from its C source under its upstream zlib-style license; enabling C in CMake is solely for this source. Both libraries lack release tags, so full revisions/digests are pinned; upstream license files remain in FetchContent source directories.

## Verification

The committed checks use no testing framework. `Scene.Cpu` needs no window/device; `Engine.PublicApi` and `Scene.Runtime` need a working Vulkan display and the supplied assets. All are independent public-header consumers (scene tests explicitly link JSON only to generate/check fixtures):

```powershell
cmake --preset debug -DGRIMLOCK_BUILD_TESTS=ON
cmake --build --preset debug --target Game EngineApiChecks SceneChecks SceneRuntimeChecks --parallel 4
ctest --test-dir out/build/debug -C Debug --output-on-failure
```

Verified on 17 October 2026:

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
