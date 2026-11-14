# Directional lighting and shadows

The Engine owns directional lighting, shadow resources, shaders, projection fitting,
filtering, and per-object casting/receiving. Game only configures these APIs and
persists the player's quality choice; Editor exposes the same APIs.

## Configuration

`Engine::setDirectionalLight` adjusts sun direction, linear color, intensity, and
enabled state. Existing ambient/environment lighting remains active in shadows.
Shadows attenuate direct sunlight only, preserving emissive materials and local lights.
No material conversion or post-processing is required.

`Engine::setShadowSettings` accepts `ShadowSettings` from `grimlock/shadows.hpp`.
The default is Medium, 2048 square, with a 3×3 PCF kernel. Game Options offers Off,
Low (1024), Medium (2048), and High (4096), restricted to supported modes. The
effective choice is saved as `shadow_resolution` in the existing options file.
Unsupported requests fall back to the highest supported mode below the request,
including Off. Sampled depth-format support and image dimensions are checked.

Default filtering and bias:

| Parameter | Default | Purpose |
| --- | --- | --- |
| PCF radius | 1 | Nine nearest depth comparisons; 0 and 2 select 1×1 and 5×5 |
| Strength | 0.8 | Retains some direct-light visibility in shadow |
| Receiver depth bias | 0.00012 | Normalized depth offset, increased at grazing angles |
| Normal bias | 0.01 | World-space offset, reduced on surfaces facing the sun |
| Raster constant / slope bias | 1 / 1.5 | Vulkan depth bias in the caster pass |
| Coverage limit | 80 | Maximum orthographic coverage radius in world units |

`Engine::setShadowProperties` configures standalone instances. Scene objects use
`Scene::setShadowProperties`; `casts_shadow` and `receives_shadow` default to true
and survive serialization and Editor duplication. Ghosts and luminous doorway,
portal, flame, and coin visuals opt out through these properties. Unlit surfaces
remain unlit regardless of receiving state.

## Rendering

A single-sample sampled depth attachment is independent of the main MSAA color,
depth, and UI composition attachments. A depth-only dynamic-rendering pass runs
before the main scene. Explicit synchronization transitions the depth image from
sampled read to depth writes and back. Quality changes replace the image after the
existing frame fence, without adding a normal-frame device-idle wait. Off releases
the map and skips caster rendering; a valid fallback descriptor remains bound.

The pass reuses uploaded meshes, material alpha masks, instance transforms, morph
weights, and skeletal deformation buffers. Animation preparation runs once, shared
by both passes. Transparent caster coverage uses a conservative alpha threshold;
dissolving geometry uses the same discard pattern as the main pass. Shadow casters
are culled conservatively against the light frustum. Pipelines and descriptors are
reused, rather than recreated per object or frame.

The orthographic camera covers a sphere around the RTS camera target, with extent
derived from camera distance, FOV, and aspect ratio. Quantized extent and light-space
texel snapping stabilize pans and rotations. The final coverage margin fades to
unshadowed lighting. Terrain elevation is included in the light depth interval.

## Editor debugging

Expand **Lighting & Shadows** in Inspector to toggle/configure the sun, quality,
PCF, strength, bias, and coverage. Sun changes are scene data; quality and bias
controls are session debugging settings. Model Inspector exposes Cast/Receive in
**Object shadows**, after the existing object controls.
**Show shadow coverage** outlines the light frustum without changing Editor layout.

**Capture shadow map** displays an Engine-provided grayscale depth snapshot when
depth transfer is supported. Refresh is explicit: readback waits for an ordered
GPU copy only when requested. Normal frames have no debug readback. The snapshot
owns a separate UI texture, survives MSAA changes, and is released on refresh or
Editor shutdown.

## Validation and limits

`Render.Shadows` exercises Level 1 terrain, all tower models, Knight/Ranger/Mage,
Goblins, Skeletons, Ghosts, and a boss. It compares lit/shadowed terrain pixels,
hard/PCF-filtered edges, and different animated poses in the depth map. It also
checks quality changes, sun disable, casting overrides, resize, all MSAA modes,
restart, and unload. `Editor.Shadows` checks controls, snapshot refresh and cleanup,
MSAA changes with the preview visible, duplication, and scene serialization.
`Game.LevelMenu` checks quality application and persistence.

Game and Editor Debug builds passed. All 51 registered checks passed across the
full regression run and the corrected Editor rerun. The regression exposed a
fixed-coordinate Inspector test and an actual animation-field scrolling regression;
the test now finds the field by ID, and optional shadow controls follow the existing
object fields to preserve their layout. The final three Editor checks passed.
Khronos core/synchronization validation reported no new errors or live-object
warnings in the renderer, MSAA/fullscreen, shadow, and Editor checks. Logs:
`out/shadows_regression.log`, `out/shadows_final_validation.log`, and
`out/shadows_editor_final_validation.log`. Formatting and whitespace checks passed.

This is one directional map, not cascades: distant regions outside coverage are
unshadowed, large views reduce effective texel density, and zoom can change the
quantized extent. Low quality still shows finite-resolution edges. Transparent
casters use cutout coverage rather than physically translucent shadows. Very
shallow sun angles or unusually scaled scenes may need bias/coverage tuning.
Changing the sun direction necessarily moves its shadow texel grid.

Run the reproducible 1080p benchmark with validation disabled:

```powershell
out/build/debug/Debug/ShadowRuntimeChecks.exe --benchmark
```

It warms each setting for 120 frames, measures 360 frames, captures each quality,
and repeats Off at the end to expose clock/thermal variation. GPU timestamp time
and total CPU frame time are reported separately; Debug FPS is CPU-sensitive.

Measured on November 12, 2026: NVIDIA GeForce RTX 3060 Ti, Debug build, 1920×1080,
4× MSAA, VSync off, validation disabled. Level 1 includes four towers, the three
animated heroes, and 19 enemy instances; 445 primitive caster draws are visible
to the light. This fixture deliberately includes more characters than Level 1's
normal opening state.

| Quality | GPU frame (ms) | Total frame (ms) | FPS |
| --- | ---: | ---: | ---: |
| Off, first | 3.000 | 10.239 | 97.7 |
| Low | 3.531 | 10.950 | 91.3 |
| Medium | 3.187 | 10.539 | 94.9 |
| High | 3.550 | 10.942 | 91.4 |
| Off, repeated | 2.719 | 10.640 | 94.0 |

Medium added approximately 0.19–0.47 ms GPU time against the two Off samples.
Clock/load variation is visible, including Low and High measuring similarly;
these short runs are not a strict ranking or a release-build performance guarantee.
Off consistently performs zero caster draws. The benchmark log and quality captures
are written under `out/` and the Debug executable directory respectively.
