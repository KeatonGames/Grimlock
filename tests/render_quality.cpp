#include "ui/game_hud.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

using namespace grimlock;
using namespace grimlock::game;

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void tick(Engine& engine, int frames = 1) {
    for (int frame = 0; frame < frames; ++frame) {
        require(engine.update(), "Rendering quality window closed");
    }
}

void checkExtent(Engine& engine, SDL_Window* window) {
    tick(engine, 6);
    int width = 0, height = 0;
    require(SDL_GetWindowSizeInPixels(window, &width, &height), "Drawable size unavailable");
    require(engine.renderingResolution() == glm::uvec2{width, height}, "Render target differs from drawable pixels");
    require(engine.performance().msaaSamples == engine.msaaSamples(),
            "Requested MSAA was not applied to the scene pipeline");
    const auto capture = engine.captureFrame();
    require(capture.width == static_cast<unsigned>(width) && capture.height == static_cast<unsigned>(height),
            "Presentation/capture was upscaled from a different resolution");
}
} // namespace

int main(int argc, char**) {
    try {
        Engine engine{EngineConfig{.windowTitle = "MSAA rendering quality checks", .width = 1920, .height = 1080}};
        const auto supported = engine.supportedMsaaSamples();
        require(!supported.empty() && supported.front() == 1, "MSAA Off is unavailable");
        const auto preferred = *std::find_if(supported.rbegin(), supported.rend(), [](auto count) {
            return count <= 4;
        });
        require(engine.msaaSamples() == preferred, "Default does not prefer supported 4x MSAA");
        engine.setMsaaSamples(8);
        require(engine.msaaSamples() == supported.back(), "Unsupported mode did not fall back safely");
        bool rejected = false;
        try {
            engine.setMsaaSamples(3);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "Invalid sample count was accepted");

        LevelRuntime runtime;
        runtime.load(engine, "assets/levels/forest_defense.json");
        GameHud hud{engine, runtime, false, {}, engine.assetRoot().parent_path() / "msaa-test-options.json"};
        engine.setVsync(false);
        engine.setPerformanceEnabled(true);
        const auto& zones = runtime.navigation().placement().zones();
        for (std::size_t index = 0; index + 1 < zones.size(); ++index) {
            const auto& zone = zones[index];
            const auto type = zone.object % 2 ? "wide" : "basic";
            (void)runtime.navigation().towers().create(
                engine,
                zone.snap(runtime.navigation().towers().definition(type).instance()));
        }
        // Keep a build circle visible and inspect a tower to cover range geometry,
        // coin triangles, text and angular UI alongside the 3D silhouettes.
        runtime.selectObject(engine,
                             Ray{zones.front().constructionPosition() + glm::dvec3{0, 20, 0}, {0, -1, 0}, 0, 40});
        require(runtime.inspectedTower(), "Range-overlay quality fixture did not select its tower");
        EnemyTypeId goblin = 0;
        while (runtime.navigation().enemies()->definition(goblin).name != "Goblin") {
            ++goblin;
        }
        require(runtime.navigation().spawnAt(engine, goblin, {{3.5, -5.5}, {11, -1}, 0}, 24) > 0,
                "Animated crowd fixture failed");
        const auto fireball =
            engine.createInstance(engine.loadModel(engine.assetRoot() / "towers/projectiles/fireball_purple.glb"),
                                  true);
        engine.setTransform(fireball, Transform{{-4, 3, 0}, {1, 0, 0, 0}, {0.2F, 0.2F, 0.2F}});
        int windowsCount = 0;
        auto** windows = SDL_GetWindows(&windowsCount);
        require(windows && windowsCount == 1, "Expected one rendering window");
        auto* window = windows[0];
        SDL_free(windows);
        std::cout << "Window pixel density: " << SDL_GetWindowPixelDensity(window) << '\n';

        std::cout << "Supported MSAA:";
        for (const auto samples : supported) {
            std::cout << ' ' << samples;
        }
        std::cout << '\n';
        for (const auto size : {glm::ivec2{1920, 1080}, glm::ivec2{1280, 720}, glm::ivec2{800, 600}}) {
            require(SDL_SetWindowSize(window, size.x, size.y), "Resize failed");
            for (const auto samples : supported) {
                engine.setMsaaSamples(samples);
                checkExtent(engine, window);
                require(hud.msaaSamples() == samples, "HUD pipeline did not follow the selected MSAA mode");
                tick(engine, 15);
                const int frames = argc > 1 ? 180 : 35;
                double gpu = 0;
                int gpuFrames = 0;
                const auto start = std::chrono::steady_clock::now();
                for (int frame = 0; frame < frames; ++frame) {
                    tick(engine);
                    if (const auto duration = engine.performance().gpuMilliseconds) {
                        gpu += *duration;
                        ++gpuFrames;
                    }
                }
                const double elapsed =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                const auto rendered = engine.renderingResolution();
                std::cout << "QUALITY " << rendered.x << 'x' << rendered.y << " samples=" << samples
                          << " frame_ms=" << elapsed / frames << " fps=" << frames * 1000 / elapsed
                          << " gpu_ms=" << (gpuFrames ? gpu / gpuFrames : -1) << '\n';
                if (argc > 1 && rendered == glm::uvec2{1920, 1080} && (samples == 1 || samples == 4)) {
                    const auto capture = engine.captureFrame();
                    const auto path =
                        engine.assetRoot().parent_path() / ("msaa-" + std::to_string(samples) + "x-1080p.png");
                    require(stbi_write_png(path.string().c_str(),
                                           capture.width,
                                           capture.height,
                                           4,
                                           capture.rgba.data(),
                                           capture.width * 4),
                            "MSAA comparison capture failed");
                }
            }
        }
        require(SDL_SetWindowFullscreen(window, true), "Fullscreen failed");
        checkExtent(engine, window);
        engine.setMsaaSamples(1);
        checkExtent(engine, window);
        require(SDL_SetWindowFullscreen(window, false), "Windowed restoration failed");
        engine.setMsaaSamples(preferred);
        require(SDL_SetWindowSize(window, 1920, 1080), "1080p restoration failed");
        checkExtent(engine, window);
        engine.removeInstance(fireball);
        runtime.unload(engine);
        tick(engine, 3);
        require(engine.performance().instances == 0, "Scene/quality recreation retained render instances");
        std::cout << "PASS MSAA modes, animated crowd, towers, HDR resolve/UI, resizing, fullscreen and cleanup.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL rendering quality: " << error.what() << '\n';
        return 1;
    }
}
