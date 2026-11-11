# Rendering quality and MSAA

The Engine prefers 4x MSAA. `Engine::supportedMsaaSamples()` reports the intersection of the GPU's color/depth framebuffer limits and the actual HDR, presentation-composition and depth image-format support, limited to Off (1), 2, 4 and 8 samples. An unsupported request falls back to the highest supported lower mode. Invalid requests are rejected. Both Game and Editor use this Engine path.

The scene uses Vulkan 1.3 dynamic rendering. With MSAA enabled, a transient multisample RGBA16F color image and matching multisample depth image cover the full drawable extent. All mesh material variants (opaque, masked, blended, mirrored and double-sided), animated/deformed meshes and the instanced editor grid use that sample count. Dynamic rendering resolves color into the existing single-sample HDR image. Tone mapping and the ImGui overlay then render into a second multisample image in the presentation color format, using the same selected sample count. The combined scene/HUD is resolved into the single-sample swapchain image. This covers gameplay and Editor UI geometry, range/build circles and the projected coin mesh without reducing resolution or blurring text with a screen-space filter. Off bypasses both additional color images and resolves.

Quality changes are queued for the next drawable frame. The renderer waits for the device, releases the old targets/command buffers, rebuilds compatible pipelines when the sample count changes, and recreates the swapchain targets. The existing minimized-window handling defers recreation until pixels are drawable. RAII owns image/view/memory cleanup. `FramePerformance::msaaSamples` reports the actual scene pipeline used by the last frame when performance diagnostics are enabled.

SDL windows request high pixel density. Swapchain size, HDR/MSAA/depth targets, viewport, scissor and camera projection use `SDL_GetWindowSizeInPixels`, not logical UI dimensions. Existing resize/pixel-size events invalidate the targets. There is no render-scale or low-resolution upscale pass. `Engine::renderingResolution()` and framebuffer captures expose the actual rendered pixel dimensions.

Textures retain authored color spaces, wrapping and minification/magnification filters. Existing linear-blit mipmap generation remains enabled when supported. Linear mipmapped material samplers use anisotropy up to 8x (clamped to GPU limits); unsupported devices and intentionally nearest/non-mipmapped samplers keep their existing filtering. Materials, palette textures and low-poly lighting are unchanged.

The Game Options menu exposes only supported MSAA modes. Apply schedules the render-resource update and persists `msaa_samples` with the existing display preferences. Missing preferences prefer 4x; obsolete/invalid values normalize to that preference before hardware fallback.

## Validation and measurement

```powershell
cmake --build --preset debug --parallel 4
ctest --test-dir out/build/debug -C Debug --output-on-failure
.\out\build\debug\Debug\RenderQualityChecks.exe --benchmark
```

`Render.Quality` switches all supported modes repeatedly at 1920x1080, 1280x720 and 800x600, checks presented/captured pixel extents and actual submitted sample counts, exercises fullscreen/windowed transitions, and unloads an animated crowd/tower scene. Benchmark mode measures 180 frames per mode after warm-up and saves Off/4x 1080p captures beside the executable. These are local measurements, not a guarantee for other GPUs or levels. `Renderer.Runtime` covers opaque, alpha-mask/blended, mirrored and deformed draw variants; `Editor.Runtime` covers Editor overlays, grid and interaction. `Game.LevelMenu` applies and reloads the MSAA preference.

To enable installed Khronos validation and synchronization checks for a local test process:

```powershell
$env:VK_INSTANCE_LAYERS = 'VK_LAYER_KHRONOS_validation'
$env:VK_LAYER_PATH = "$env:VULKAN_SDK/Bin"
$env:VK_LAYER_VALIDATE_SYNC = '1'
.\out\build\debug\Debug\RenderQualityChecks.exe
```

MSAA smooths geometry coverage; it does not provide temporal antialiasing. Very thin distant geometry, alpha-test texture boundaries and very small font/texture details can still alias. Authored nearest-filtered textures remain intentionally nearest-filtered.

## Original scene-only MSAA results (RTX 3060 Ti)

The GPU exposes Off, 2x, 4x and 8x for the selected HDR/presentation/depth formats; the default is 4x. Material anisotropy is capped at 8x. Before the UI composition pass was multisampled, the Debug benchmark below used VSync off, the Forest Defense scene, four tower defenders, 24 animated Goblins, a projectile model and the native-resolution HUD. Each mode warmed up before 180 measured frames. GPU timestamps exclude CPU work; total frame time includes CPU preparation and presentation.

| 1920x1080 mode | GPU time | Total frame time | FPS |
| --- | ---: | ---: | ---: |
| Off | 1.189 ms | 7.569 ms | 132.1 |
| 2x | 1.506 ms | 7.670 ms | 130.4 |
| 4x | 1.825 ms | 7.902 ms | 126.5 |
| 8x | 1.787 ms | 7.714 ms | 129.6 |

In this run 4x added about 0.64 ms of GPU work and 0.33 ms total frame time (roughly 4% lower FPS). These short runs have clock/scheduling variance, so the small 4x/8x ordering difference is not a performance ranking. These historical scene-only numbers do not include the later UI resolve. Full mode/size measurements are in `out/msaa_benchmark_verified.log`; Off and 4x comparison PNGs are in `out/build/debug/Debug/msaa-1x-1080p.png` and `msaa-4x-1080p.png`.

Khronos core/synchronization validation reported no errors or live-device-object warnings in the quality, renderer-material/deformation/grid, and Editor checks. Logs are `out/msaa_quality_verified_validation.log`, `out/msaa_renderer_verified_validation.log` and `out/msaa_editor_verified_validation.log`. Resize and fullscreen captures matched the SDL drawable pixel extent. This display reported 1x pixel density; moving between physical monitors with different DPI was not available to test.

The broader regression run also exposed older fixture assumptions about short tower ranges, the defender firing offset and wave-only gold. Those fixtures now cover the current authored ranges, actual shot segments and immediate kill rewards; no gameplay tuning was changed for MSAA. The final focused rerun is recorded in `out/msaa_regression_reruns.log`.

Game and Editor Debug builds succeeded. All 48 registered checks were verified across the full run and the corrected focused reruns; the final eight affected reruns passed. Formatting and whitespace checks also passed.
## Scene and UI MSAA

The selected mode now covers both the 3D scene and final UI composition. `OverlayTarget::samples` informs the shared Game/Editor ImGui backend, and its main graphics pipeline is recreated when the sample count changes. Quality-only switches retain font/image textures so draw data generated earlier in the frame remains valid. The tone-mapping pipeline matches this composition sample count, and the final presentation resolve happens after the overlay. No font-resolution reduction or screen-space blur is used.

`Render.Quality` also checks the HUD's active sample count and leaves a build zone visible while inspecting a tower, covering range circles, build circles, coin triangles, text and panel edges. Khronos core/synchronization validation passed for these shapes across supported modes, resizing and fullscreen. Renderer and Editor validation passed as well. Current logs use `out/ui_msaa_*`.

A new Debug 1080p comparison with this expanded fixture (three tower defenders, 24 Goblins, visible range/build overlays, projectile and HUD) measured 1.50 ms GPU / 9.72 ms total with MSAA Off, versus 2.11 ms GPU / 9.67 ms total at 4x. The run was CPU-bound at about 103 FPS; short-run CPU/GPU-clock noise should not be interpreted as a speedup. These numbers are not directly comparable to the older fixture above. Details are in `out/ui_msaa_benchmark.log`; the Off/4x PNGs beside the executable now show the combined scene-and-UI result.
