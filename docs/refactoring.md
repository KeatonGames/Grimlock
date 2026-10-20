# Incremental refactoring

Review baseline: 90 project-owned C++ files. The priorities below reflect mixed
responsibilities, nesting, verification coverage, and the risk of each change;
function size alone is not a refactoring target.

| Order | Module | Scope | Status |
| --- | --- | --- | --- |
| 1 | glTF primitive conversion | Separate attribute decoding, deformation data, triangle preparation, and normal mapping in `gltf_loader.cpp::readPrimitive`. | Complete |
| 2 | glTF model assembly | Separate parsing/validation, nodes, meshes, and hierarchy checks in `loadGltf`; keep storage validation ordering. | Complete |
| 3 | glTF animation and material import | Separate skin/light/clip conversion and sampler decoding, then material conversion and referenced-image loading in separate iterations. | Complete |
| 4 | Vulkan frame recording | Separate draw preparation, ordering, and command recording in `VulkanRenderer::recordFrame`; preserve barriers and resource lifetimes. | Complete |
| 5 | Editor dialogs | Separate delete, file, unsaved-change, and error popups in `Editor::dialogs`; preserve pending-operation state. | Complete |
| 6 | Animation evaluation | Separate sampling, local/world pose updates, and skin matrices in `ModelInstance::evaluate`; preserve math and allocations. | Complete |
| 7 | Scene serialization and Engine coordination | Separate scene JSON conversion and Engine resource/frame preparation; preserve validation, activation, and cleanup order. | Complete |

Other reviewed areas include picking, grid tools, OBJ/image loading, environment
preprocessing, input, cameras, Vulkan resource setup (including `MeshPipeline`),
and the Game launcher. Their existing behavior and boundaries remain unchanged;
reassess them after the higher-priority work rather than refactoring by size.

Refactor one module per explicit request, then build, verify, record progress,
and stop. Preserve completed iterations and existing work.

The final request completed Scene and Engine work together, followed by a review
of all 96 project-owned C++ files. All planned modules are complete. The records
below retain each iteration's scope and verification results.

## Iteration 1: glTF primitive conversion

- Completed: `readPrimitive` coordinates nine anonymous-namespace helpers:
  `validatePrimitiveAttributes`, `readVertexGeometry`, `readTextureCoordinates`,
  `readVertexTangents`, `readSkinWeights`, `readMorphTargets`,
  `readTriangleIndices`, `prepareTriangleGeometry`, and `prepareNormalMapping`.
- Guard clauses flatten missing-normal, missing-tangent, and normal-map fallback
  paths. Joint/weight accessor and vertex names now describe the data they hold.
- Retained decoding order, validation messages, normal/tangent math, skin-weight
  normalization, morph indexing, degenerate filtering, and vertex seam behavior.
  Helpers add no allocations or public APIs. Vulkan ownership and synchronization
  are untouched; `loadGltf` and storage validation remain unchanged.
- Added `Assets.GltfPrimitive` CPU coverage for indexed/non-indexed triangles,
  mixed/missing/valid normals, present/absent tangents, both UV sets, eight skin
  influences, reordered morph indices after flat-vertex preparation, degenerate
  filtering, and invalid joints/weights, topology, and tangent handedness.
- Verification succeeded: complete Debug and Release builds; all 11 CTest checks
  passed in each configuration, including CPU, Vulkan, and editor runtime checks.
  The strengthened CPU regression also passed its final Debug rerun. clang-format
  verification and `git diff --check` passed. No additional setup is required.
- Scope review: `gltf_loader.cpp` is the only pre-existing C++ file changed in this
  iteration, confined to primitive conversion. Test registration and this progress
  document accompany the new regression test. Existing formatting work is intact.

## Iteration 2: glTF model assembly

- Completed: `loadGltf` coordinates anonymous-namespace helpers `parseGltfAsset`,
  `validateGltfAsset`, `readModelNodes`, `selectModelRoots`, `readModelMeshes`,
  `bindModelNodes`, and `resolveBindPosePreviews`. Skin validation and preview
  detection are isolated in `validateSkinnedPrimitive` and
  `isBindPosePreviewCandidate`, reducing nesting in node binding.
- Scene fallback and preview checks use early returns. Node, quaternion, matrix,
  and skin-lane names describe their purpose; matrix comparisons retain the same
  iteration order and tolerances.
- Preserved parser/backing-buffer lifetimes, validation order, selected-scene
  policy, primitive sharing, morph defaults, skin/light checks, diagnostics, and
  bounds. No public APIs, extra vertex scans, or mesh-data copies were added.
- Added `Assets.GltfModel` regressions for scene selection/fallback, hierarchy
  transforms, shared primitive mapping, node/mesh/zero morph defaults, skin/light
  binding, both preview outcomes, validation precedence, and error context.
  These passed against the original loader before extraction and after refactoring.
- Verification succeeded: complete Debug and Release builds with Editor and tests
  enabled; all 12 CTest checks passed in each configuration, including CPU,
  Vulkan, and editor runtime checks. clang-format verification and
  `git diff --check` passed. No additional setup is required.
- Scope review: `gltf_loader.cpp` is the only pre-existing C++ file changed this
  iteration. Primitive conversion and storage validation from iteration 1 are
  unchanged; animation/material importer implementations and renderer ownership
  and synchronization are untouched. Test registration and this progress record
  accompany the new regression test.
- Next requested iteration: animation import (`loadGltfAnimation`); review material
  import separately afterward. No work on either importer was started here.

## Iteration 3: glTF animation import

- Completed: `loadGltfAnimation` coordinates private `readSkin`, `readLight`, and
  `readAnimationClip` helpers. `readAnimationSampler` separates accessor validation
  and timing from channel validation; `readAnimationValues` decodes scalar, vector,
  and quaternion outputs. `readAnimationPath` and `readInterpolation` isolate enum
  conversion, and a private throwing helper retains existing error handling.
- Guard clauses flatten default inverse-bind handling and output decoding. Clear
  node, matrix, color-component, joint-set, and key-time names replace terse locals.
  Input/output accessors now have separate declarations.
- Preserved skin/light/clip order, lazy sampler decoding and reuse, skipped channels
  without target nodes, morph component counts, cubic in/value/out ordering, unit
  quaternion checks, validation precedence, profiling, and error messages. Helpers
  introduce no public APIs, extra decoding passes, or copied sample arrays.
- Added `Assets.GltfAnimation` CPU regressions for all channel paths and interpolation
  modes, compatible/incompatible sampler reuse, clip ranges, unused samplers and
  skipped channels, cubic quaternion tangents, morph ordering, skin/light defaults,
  explicit inverse binds, and invalid channels/accessors/times/values/resources.
  The same checks passed against the original importer before extraction and the
  refactored importer afterward. Tests use the existing fastgltf dependency.
- Verification succeeded: complete Debug and Release builds with Editor and tests
  enabled; all 13 CTest checks passed in each configuration, including CPU,
  Vulkan, and editor runtime checks. clang-format verification and
  `git diff --check` passed. No additional setup is required.
- Scope review: `gltf_animation.cpp` is the only pre-existing C++ file changed this
  iteration. Earlier loader/primitive work, material import, runtime animation
  evaluation, public headers, and Vulkan code are unchanged. Test registration and
  this progress record accompany the new regression test.
- Next requested iteration: material import (`loadGltfMaterials`). Stop here until
  explicitly requested to continue.

## Iteration 4: glTF material import

- Completed: `loadGltfMaterials` coordinates private `readModelTextures`,
  `readMaterial`, and `loadMaterialImages` helpers. Material conversion separates
  `readMaterialExtensions`, `validateMaterialExtensions`, and `readMaterialSurface`.
  Existing reference, sampler, and image-source helpers are unchanged.
- A guard clause skips cached images before decoding. A named texture reference
  replaces repeated optional-pointer dereferences; component names clarify color
  conversion and validation. No new public APIs or abstractions were introduced.
- Preserved validation order, texture-slot order, lazy image decoding/cache reuse,
  material/default ordering, optional extension warnings, sampler/UV behavior,
  profiling, ownership, and image-error context. Conversion adds no decoding passes
  or image copies, and renderer algorithms and synchronization are unchanged.
- Added `Assets.GltfMaterials` CPU regressions for core and extension fields, all
  14 texture slots, UV transforms/overrides, alpha modes, six minification filters,
  wrap/magnification modes, default materials/samplers, shared and predecoded
  images, unused-image handling, warnings, invalid factors/references, and error
  precedence/context. The same checks passed against the original importer before
  extraction and the refactored importer afterward. No dependencies were added.
- Verification succeeded: complete Debug and Release builds with Editor and tests
  enabled; all 14 CTest checks passed in each configuration, including CPU,
  Vulkan, and editor runtime checks. clang-format verification and
  `git diff --check` passed. No additional setup is required.
- Scope review: `gltf_materials.cpp` is the only pre-existing C++ file changed this
  iteration. Earlier refactoring, image decoders, public headers, and renderer code
  are unchanged. Test registration and this progress record accompany the new
  regression test.
- Next requested iteration: Vulkan frame recording (`VulkanRenderer::recordFrame`).
  Stop here until explicitly requested to continue.

## Iteration 5: Vulkan frame recording

- Completed: `recordFrame` coordinates private `preparePrimitiveDraws`,
  `sortPrimitiveDraws`, and `recordPrimitiveDraws` helpers. Anonymous-namespace
  `validateModelDraw` and `validateNodeTransform` flatten the nested preparation
  loops. The existing `PrimitiveDraw` type moved to the renderer's private section;
  its fields and the public API are unchanged.
- Kept barriers, render-pass transitions, tone mapping, and overlay placement
  together in `recordFrame`. Source comparison confirms those sections, the sort
  comparator, binding caches, deformation upload, and primitive command sequence
  are unchanged. Submission, swapchain handling, and cleanup are untouched.
- Preserved validation precedence, transform/normal math, primitive insertion
  order, transparent sorting/ties, deformation append order, and resource lifetimes.
  Helpers add no frame allocations, resource copies, or additional traversal passes.
- Added `Renderer.Runtime` coverage for opaque/masked/blended materials, both-sided
  and mirrored draws, shared mesh nodes, material overrides, independent supplied
  poses, rest-pose fallback, skin/morph deformation, empty frames, resize, and draw
  validation/precedence. These checks passed against the original and refactored
  implementations.
- Verification succeeded: complete Debug and Release builds with Editor and tests
  enabled; all 15 CTest checks passed in each configuration with Vulkan core and
  synchronization validation enabled. Captured logs contain no Vulkan validation
  errors. Seven frames captured with the installed SDK screenshot layer match the
  original implementation byte-for-byte, including ordering, pose, and resize cases.
  Logs and captures are under the ignored `out/refactoring-iteration-5/` directory.
  clang-format verification and `git diff --check` passed. No dependencies or new
  external setup requirements were introduced.
- Scope review: only `vulkan_renderer.cpp` and its internal header changed among
  pre-existing C++ files. Earlier iterations and other modules are unchanged.
  Test registration and this progress record accompany the new runtime check.
- Next requested iteration: Editor dialogs (`Editor::dialogs`). Stop here until
  explicitly requested to continue.

## Iteration 6: Editor dialogs

- Completed: `Editor::dialogs` coordinates private `deleteDialog`,
  `sceneFileDialog`, `unsavedChangesDialog`, and `errorDialog` helpers after the
  existing grid dialog. `sceneFileList` handles browsing separately, and
  `saveSceneFromDialog` shares save-result and inline-error handling.
- Guard clauses flatten closed-popup handling and scene-file filtering. Sharing
  save handling removes duplicated callbacks and simplifies continuation branches.
  ImGui popup/list-box begin/end calls remain balanced; widget IDs, labels, layout,
  and popup processing order are unchanged.
- Preserved pending operations/paths, deferred Save As continuation, distinct
  cancellation state changes, inline errors, deletion, and cleanup. Existing
  request/execution, document commands, frame handling, public APIs, and stored
  state are unchanged; no new UI or abstractions were added.
- Extended `Editor.Runtime` through actual menu, modal, and text-input interactions
  for dirty New/Open cancellation, failed Save As, cancellation of a deferred save,
  successful Save As followed by New Scene, pending Open/Discard paths, failed Open
  preserving the active document, and error dismissal. Existing deletion and close
  checks remain intact. The expanded check passed against the original code before
  extraction and the refactored implementation afterward.
- Verification succeeded: complete Debug and Release builds with Editor and tests
  enabled; all 15 CTest checks passed in each configuration with Vulkan core and
  synchronization validation enabled. Captured logs contain no Vulkan validation
  errors. clang-format verification and `git diff --check` passed. No dependencies,
  CMake changes, or additional external setup were needed.
- Scope review: production edits are confined to dialog rendering and private
  helpers in `editor.cpp`/`editor.hpp`; the existing Editor runtime check and this
  progress record accompany them. Source/hash comparison confirms other Editor
  methods and earlier modules are unchanged.
- Next requested iteration: animation evaluation (`ModelInstance::evaluate`). Stop
  here until explicitly requested to continue.

## Iteration 7: animation evaluation

- Completed: `ModelInstance::evaluate` coordinates private `resetPoseChannels`,
  `applyAnimationChannel`, `updateNodeTransforms`, and `updateSkinMatrices`
  helpers. An anonymous-namespace `isFinite` shares matrix validation and removes
  duplicated nested loops from transform and skin updates.
- Channel application uses a guard return for linear quaternion interpolation.
  Key, interval, component, node, and joint names clarify sampling and matrix
  construction. Helpers retain existing scratch buffers and add no allocations,
  pose copies, traversal passes, public APIs, or stored state.
- Preserved interpolation arithmetic/order, quaternion normalization, channel
  ordering, morph defaults, explicit matrices, parent-first world updates,
  selected-scene membership, mesh-local skin palettes, bind-pose previews, and
  validation diagnostics/order. All world transforms still finish before skin
  matrices are evaluated. Constructor and playback methods are unchanged.
- Added `Animation.Cpu` regressions for step/linear/cubic sampling, nonuniform
  intervals and key boundaries, single keys, quaternion modes/tangents, morph
  weights, clip/rest-pose transitions, independent instances, shared asset data,
  selected/unselected hierarchy nodes, mirrored/nonuniform skin transforms,
  inverse binds, preview palettes, pose-buffer reuse, and evaluation errors.
  These checks passed against the original evaluator before extraction and the
  refactored evaluator afterward.
- Verification succeeded: complete Debug and Release builds with Editor and tests
  enabled; all 16 CTest checks passed in each configuration with Vulkan core and
  synchronization validation enabled. Captured logs contain no Vulkan validation
  errors. The final Debug animation regression rerun, clang-format verification,
  and `git diff --check` passed. Logs are under `out/refactoring-iteration-7/`.
  No dependencies or additional external setup were needed.
- Scope review: only `animation.cpp` and private declarations in `animation.hpp`
  changed among the 95 pre-existing C++ files checked this iteration. The new CPU
  check, its CMake registration, and this progress record accompany them.
  Existing Editor changes and earlier modules are intact.
- Next requested iteration: Scene serialization (`Scene::load`/`save`); review
  Engine coordination separately afterward. Stop here until explicitly requested
  to continue.

## Iteration 8: Scene serialization and Engine coordination

- Completed: `Scene::load` delegates bounded JSON reading/parsing to
  `readSceneDocument` and property conversion to `readObjectProperties`.
  `Scene::save` delegates grid, object, camera, and light records to `writeGrid`,
  `writeObject`, `writeCamera`, and `writeLighting`; `writeSceneDocument` retains
  serialization before opening the destination. Helpers stay in the implementation
  file, with no JSON types or new dependencies in public headers.
- Preserved object insertion before parent linking, forward references, stable
  IDs, camera selection/order, authored animation settings, grid/light records,
  schema limits, error context/precedence, and validation before writing.
- `Engine::Impl::prepare` coordinates `bindSceneModels` and
  `validateSceneFrameLimits`. `update` delegates event processing to `pollEvents`
  and standalone animation/lighting to `standaloneFrame`. Clear counter names and
  const validation helpers make the stages easier to follow.
- Preserved frame sequencing, resource-cache behavior, activation rollback,
  ownership, member destruction order, limits, and cleanup. Public Engine methods
  and resource collection are unchanged; extraction adds no frame allocations,
  model copies, or traversal passes. Vulkan command recording/synchronization and
  animation math are unchanged.
- Expanded `Scene.Cpu` for forward references, camera/map ordering, light
  attachments, explicit next IDs, validation precedence/context, and preserving
  an existing destination when serialization fails. Extended `Scene.Runtime` for
  mixed standalone imported-light/static instances and scene/unload transitions.
  These regressions passed before and after extraction. All 17 Scene/Grid fixture
  files match the original implementation byte-for-byte.

## Final codebase review

- Reviewed all 96 project-owned C++ files across Engine, Game, Editor, and tests,
  including earlier refactoring iterations. Retained readable code and existing
  algorithms in asset import, animation, cameras/input, grid tools, launchers, and
  Vulkan resource/pipeline setup rather than splitting functions by size.
- Shared the Editor's duplicated transform-precision check in a file-local
  `preservesTransform` helper, retaining arithmetic, tolerances, and diagnostics.
  Extracted Engine nearest-hit picking into `pickSceneObject` with an invalid-ray
  guard; input policy and draw highlighting remain in `updateSelection`.
  Flattened triangle-hit/alpha rejection in `Geometry::intersect`. Added the direct
  standard algorithm include in `gpu_mesh.cpp` and corrected material-uniform
  component documentation.
- This pass changed eight C++ files (six production, two existing tests), plus
  this progress record. No project files, dependencies, public APIs, or CMake/style
  configuration changes were needed. Earlier Editor and animation work is intact.
- Final verification succeeded: complete Debug and Release builds with Editor
  and tests enabled; all 16 CTest checks passed in each configuration, including
  CPU, rendering, picking, camera, resource-stress, and Editor interactions.
  Vulkan core and synchronization validation were enabled; captured logs contain
  no validation errors. Modified C++ files were formatted, all 96 files passed
  clang-format verification, and `git diff --check` passed.
- Build/test logs, baseline snapshots, and the scope check are in the ignored
  `out/refactoring-final/` directory. No additional external setup is required;
  no planned modules remain.
