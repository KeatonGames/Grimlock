# Color, materials, and texture quality

All reusable color transfer, material evaluation, texture upload, mip generation,
sampler configuration, and tone mapping live in Engine. Game persists preferences
and Editor presents the Engine APIs; neither owns a rendering implementation.

## Color audit

The existing pipeline already sampled base-color, emissive, and specular-color
images as sRGB, and normal/metallic/roughness/occlusion/weight images as linear
UNORM. An image used in both roles retains separate GPU uploads. glTF material
factors and instance tints are linear and are not gamma-corrected on import.
Directional, local, and environment lighting accumulate into a linear RGBA16F
target, with transparency blended before tone mapping. Shadow visibility affects
direct sunlight, preserving ambient and emissive lighting.

Tone mapping precedes the sole output transfer, performed by the sRGB presentation
attachment. Unsupported non-sRGB surfaces are rejected explicitly instead of
silently presenting incorrect colors. No arbitrary gamma or exposure compensation
was added. Existing UI composition remains after scene tone mapping, at native
resolution with the chosen MSAA count. Font/texture sampling remains independent
of model filtering preferences.

The audit found that `COLOR_0` was previously ignored. Engine now imports linear
VEC3/VEC4 colors (float or normalized unsigned bytes/shorts), defaults absent colors
to white and VEC3 alpha to one, and preserves them across flat-normal and tangent
splits. The GPU multiplies vertex color with the material, texture, and instance
color. Masked shadows also use vertex alpha. Color values/counts are validated.

`grimlock/color.hpp` provides shared sRGB transfer functions, used by mip fallback
and both application palettes. Alpha is never passed through the RGB transfer.
The Engine ImGui bridge also owns `prepareSrgbPixels`, moving the existing menu
artwork conversion out of Game. It decodes source RGB once for the backend's
UNORM upload and leaves font atlases, linear debug images, and alpha untouched.
Existing inverse-transpose normal handling, mirrored transforms, normal maps,
metallic/roughness evaluation, alpha modes, and animated deformation are retained.

## Texture sampling

Textures normally generate their full mip chain through linear Vulkan blits.
The fallback now generates and uploads a complete CPU chain instead of dropping
to one level when blits are unavailable. Color mips average in linear light and
encode back to sRGB; data and alpha average linearly. Area weights retain edge
texels in odd and non-square dimensions. Explicit barriers transition all uploaded
levels to shader-read layout. Unsupported linear texture filtering falls back to
nearest sampling without rejecting an otherwise usable texture.

Engine exposes `RenderSettings`, `setRenderSettings`, `renderSettings`, and
`supportedAnisotropy`. Bilinear uses bilinear texels with nearest mip selection;
Trilinear interpolates mip levels; Anisotropic adds supported anisotropy. The
Engine default remains Anisotropic 8x, normalized to device support; Game explicitly
requests 16x with the same safe fallback. Supported
power-of-two values through 16x are offered. Explicit nearest/palette samplers
retain their authored filters and wrap modes. Linear samplers receive mip filtering
without needing per-asset edits.

Each model shares samplers among equivalent texture slots and shares images across
materials and instances. Changing filtering waits for the existing frame fence,
replaces samplers/descriptors, and keeps all texture images, meshes, and material
buffers. No normal-frame sampler creation, texture reload, or extra device-idle
wait is introduced. Shadow comparison and environment-prefilter samplers are not
changed by material-filtering preferences.

## Tone mapping and controls

HDR already existed, so no new HDR architecture was introduced. The existing
Narkowicz filmic fit remains the default; it is not a complete ACES color pipeline.
Reinhard and Linear Clamp are optional Engine operators using the same pass and
exposure. Linear Clamp can clip highlights above one. Tone selection is a push
constant and does not rebuild pipelines or affect UI colors.

Game uses fixed Anisotropic 16x and Filmic defaults. Unsupported anisotropy requests
normalize to the highest supported lower level, or Trilinear without anisotropy
support. Player Options retains display, VSync, MSAA, and shadow controls; technical
filtering and tone controls are absent. Legacy `texture_filtering`, `anisotropy`,
and `tone_mapping` preferences are ignored, including incompatible old value types,
and omitted on the next options save.

Editor has a collapsed **Rendering Settings** section after the existing Inspector
fields. One Texture Filtering selector offers Bilinear / Trilinear / Anisotropic;
supported 2x/4x/8x/16x levels appear only with Anisotropic selected. Tone Mapping
offers Linear Clamp / Reinhard / Filmic. Changes preview immediately through the
existing Engine API and remain Editor session settings. A read-only **Materials**
section exposes base-color swatches, metallic/roughness, alpha mode, unlit state,
and sidedness using Engine-owned asset data. These controls do not edit GLBs or
introduce a new material system.

## Checks and limitations

`Render.MaterialQuality` checks sRGB transfer, linear versus color mip averages,
odd extents, forced CPU fallback upload, constant texture colors across filtering
modes, linear vertex/material multiplication, and all tone operators using GPU
pixel readback. Minified checkerboard samples verify that bilinear, trilinear,
and grazing-angle anisotropic sampling differ, while authored nearest samplers
remain unchanged. Vertex-alpha masking and blending are also checked.
`Assets.GltfPrimitive` covers normalized RGBA, RGB/default alpha,
missing colors, invalid color encoding, and geometry splitting. Existing renderer,
shadow, MSAA, UI, Editor, and level-menu checks exercise integration and persistence.
The level-menu test also loads malformed legacy technical values, verifies fixed
Game defaults and removed controls, and checks that saved options drop obsolete
keys. The Editor test exercises all filtering/tone choices and supported anisotropy
through actual viewport controls.

The settings cleanup built Game and Editor successfully. `Game.LevelMenu`,
`Editor.Shadows`, `Render.Quality`, and `Render.MaterialQuality` passed with
Khronos core/synchronization validation and no new errors. This covers legacy
preference handling, removed Game controls, Editor viewport previews, MSAA,
shadows, and material filtering/tone operators. Logs are
`out/graphics_settings_game_checks.log` and `out/graphics_settings_editor_checks.log`.

Nearest/palette textures intentionally retain their appearance. Filtering cannot
fix atlas padding or restore detail missing from source textures. Alpha-tested
mip coverage is averaged normally, without a separate coverage-preservation pass.
CPU fallback is slower at asset load time and can differ slightly from bilinear
GPU blits on odd dimensions. UI raster artwork retains the existing backend's
8-bit linear upload precision. Existing transmission, volume, and sheen limitations
remain; no new material layers or post-processing were introduced.

## Measured validation

Game and Editor Debug builds passed. Fifteen focused integration checks passed
with Khronos core/synchronization validation, followed by the expanded material
pixel test and final Game menu / Editor runtime / Editor snapshot reruns. No new
validation errors or live-object warnings were reported. Settings application and
reload initially covered anisotropy 16x and Linear Clamp before the Game/Editor
settings cleanup; current Game defaults and legacy migration are tested separately.
MSAA and shadow choices survived reload. Logs are `out/material_integration_validation.log`,
`out/material_final_pixel_details.log`, and `out/material_final_ui_details.log`.

November 12, 2026, RTX 3060 Ti, Debug, 1920x1080, VSync and validation off:
the same Level 1 fixture includes four towers, three animated heroes, and 19 enemy
instances. These pre-cleanup measurements used 4x MSAA, Medium shadows, Filmic,
and anisotropy 8x; Game now requests 16x as described above.
Hardware support is 1/2/4/8/16x anisotropy; startup logging reports the hardware
ceiling, not the selected default. Each quality sample warms 120 frames and measures
360 frames. The default Medium-shadow measurements were:

| Run | GPU frame (ms) | Total frame (ms) | FPS |
| --- | ---: | ---: | ---: |
| Before | 3.807 | 9.566 | 104.5 |
| After | 3.765 | 10.381 | 96.3 |
| After, repeated | 3.659 | 9.379 | 106.6 |

GPU time stayed close to baseline. CPU/scheduling variation is visible in total
frame times; these short Debug runs do not establish a speedup or a systematic
regression. Full per-shadow-quality measurements are in
`out/material_quality_before.log`, `out/material_quality_after.log`, and
`out/material_quality_after_repeat.log`. Default Level 1 captures were inspected
for grass, cliffs, decorations, towers, characters, shadows, and crisp UI. The
grazing-angle synthetic texture test verifies filtering behavior independently of
the level's mostly flat-color and palette textures.
