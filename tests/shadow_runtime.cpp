#include "renderer/shadow_camera.hpp"
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
    for (int i = 0; i < frames; ++i) {
        require(engine.update(), "Shadow test window closed");
    }
}

void projectionChecks() {
    Camera camera;
    camera.target = {0, 0, 0};
    camera.position = {20, 25, -25};
    DirectionalLight sun;
    ShadowSettings settings;
    const auto first = renderer::shadowUniforms(camera, 16.0F / 9, sun, settings, 2048);
    camera.target.x += 0.001F;
    camera.position.x += 0.001F;
    const auto panned = renderer::shadowUniforms(camera, 16.0F / 9, sun, settings, 2048);
    for (int i = 0; i < 4; ++i) {
        require(glm::length(first.viewProjection[i] - panned.viewProjection[i]) < 0.0001F,
                "Sub-texel camera pan destabilized shadow projection");
    }
    sun.direction = {0, -1, 0};
    const auto vertical = renderer::shadowUniforms(camera, 1, sun, settings, 2048);
    require(std::isfinite(vertical.viewProjection[0][0]), "Vertical sun generated a singular light camera");
    require(renderer::shadowIntersects(glm::mat4{1}, {0, 0, 0.5F}, 0.1F) &&
                !renderer::shadowIntersects(glm::mat4{1}, {8, 0, 0.5F}, 0.1F),
            "Shadow frustum culling failed");
    settings.pcfRadius = 3;
    bool rejected = false;
    try {
        validateShadowSettings(settings);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "Invalid PCF radius accepted");
}

void save(Engine& engine, const char* name) {
    const auto capture = engine.captureFrame();
    const auto path = engine.assetRoot().parent_path() / name;
    require(
        stbi_write_png(path.string().c_str(), capture.width, capture.height, 4, capture.rgba.data(), capture.width * 4),
        "Shadow capture failed");
}
} // namespace

int main(int argc, char**) {
    try {
        projectionChecks();
        Engine engine{EngineConfig{.windowTitle = "Directional shadow checks", .width = 1920, .height = 1080}};
        engine.setPerformanceEnabled(true);
        LevelRuntime runtime;
        runtime.load(engine, "assets/levels/forest_defense.json");
        GameHud hud{engine, runtime, false, {}, engine.assetRoot().parent_path() / "shadow-test-options.json"};
        engine.setVsync(false);
        const auto qualities = engine.supportedShadowQualities();
        auto settings = engine.shadowSettings();
        require(!qualities.empty() && qualities.front() == ShadowQuality::off, "Off is unavailable");
        for (const auto& zone : runtime.navigation().placement().zones()) {
            (void)runtime.navigation().towers().create(
                engine,
                zone.snap(runtime.navigation().towers().definition(zone.object % 2 ? "wide" : "basic").instance()));
        }
        std::vector<InstanceId> heroes;
        const auto definitions = loadHeroDefinitions(engine.assetRoot());
        for (std::size_t i = 0; i < definitions.size(); ++i) {
            const auto& definition = definitions[i];
            const auto model = engine.loadModel(engine.assetRoot() / definition.model);
            const auto info = engine.modelInfo(model);
            const auto instance = engine.createInstance(model, true);
            const auto foot = glm::vec3{(info.minimum.x + info.maximum.x) * 0.5F,
                                        info.minimum.y,
                                        (info.minimum.z + info.maximum.z) * 0.5F};
            const float scale = definition.visualHeight / (info.maximum.y - info.minimum.y);
            engine.setTransform(instance,
                                Transform{glm::vec3{-8.0F + static_cast<float>(i), 2, 10.5F} - foot * scale,
                                          {1, 0, 0, 0},
                                          glm::vec3{scale}});
            const auto clip =
                std::find(info.animationNames.begin(), info.animationNames.end(), definition.walkAnimation);
            require(clip != info.animationNames.end(), "Hero movement animation missing");
            engine.animation(instance).play(static_cast<std::size_t>(clip - info.animationNames.begin()), true);
            heroes.push_back(instance);
        }
        // Keep one walking character in an open patch for pose-dependent map checks.
        auto heroTransform = engine.transform(heroes.front());
        heroTransform[3].x = 3.5F;
        heroTransform[3].y = 0;
        heroTransform[3].z = -1;
        engine.setTransform(heroes.front(), heroTransform);
        for (const auto* name : {"Goblin", "Skeleton", "Ghost", "Goblin Boss"}) {
            EnemyTypeId type = 0;
            while (type < runtime.navigation().enemies()->typeCount() &&
                   runtime.navigation().enemies()->definition(type).name != name) {
                ++type;
            }
            require(type < runtime.navigation().enemies()->typeCount(), "Shadow enemy fixture missing");
            if (std::string_view{name} == "Ghost") {
                require(!runtime.navigation().enemies()->definition(type).castsShadow,
                        "Ghost shadow policy was ignored");
            }
            require(runtime.navigation().spawnAt(engine,
                                                 type,
                                                 {{4, -6}, {11, -1}, 0},
                                                 std::string_view{name} == "Goblin Boss" ? 1 : 6) > 0,
                    "Animated enemy shadow fixture failed");
        }
        engine.setAnimationsPaused(true);
        settings.quality = ShadowQuality::off;
        engine.setShadowSettings(settings);
        tick(engine, 3);
        const auto unshadowed = engine.captureFrame();
        settings.quality = ShadowQuality::medium;
        engine.setShadowSettings(settings);
        tick(engine, 3);
        const auto soft = engine.captureFrame();
        std::size_t shadedTerrain = 0;
        for (std::size_t i = 0; i < soft.rgba.size(); i += 4) {
            if (unshadowed.rgba[i + 1] > unshadowed.rgba[i] * 1.1 &&
                unshadowed.rgba[i + 1] > unshadowed.rgba[i + 2] * 1.1 &&
                soft.rgba[i + 1] + 8 < unshadowed.rgba[i + 1]) {
                ++shadedTerrain;
            }
        }
        require(shadedTerrain > 500, "Shadow pass did not visibly shade the terrain");
        settings.pcfRadius = 0;
        engine.setShadowSettings(settings);
        tick(engine, 2);
        const auto hard = engine.captureFrame();
        std::size_t filteredPixels = 0;
        for (std::size_t i = 0; i < hard.rgba.size(); i += 4) {
            if (std::abs(int(hard.rgba[i + 1]) - int(soft.rgba[i + 1])) > 2) {
                ++filteredPixels;
            }
        }
        require(filteredPixels > 100, "PCF filtering did not change shadow edges");
        if (engine.shadowDebugInfo().readbackAvailable) {
            const auto poseA = engine.captureShadowMap();
            require(poseA.width == engine.shadowDebugInfo().resolution, "Depth-map snapshot size is incorrect");
            const auto& clip =
                engine.animation(heroes.front()).model().animations[engine.animation(heroes.front()).clip()];
            engine.animation(heroes.front()).seek((clip.start + clip.end) * 0.5F);
            tick(engine, 2);
            const auto poseB = engine.captureShadowMap();
            require(poseA.rgba != poseB.rgba, "Character shadow remained in a static pose");
        }
        settings.pcfRadius = 1;
        engine.setShadowSettings(settings);
        engine.setAnimationsPaused(false);
        std::cout << "SHADOW GPU samples=" << engine.msaaSamples() << " supported:";
        for (auto quality : qualities) {
            std::cout << ' ' << static_cast<unsigned>(quality);
        }
        std::cout << '\n';
        auto measuredQualities = qualities;
        if (argc > 1) {
            measuredQualities.push_back(ShadowQuality::off);
        }
        for (auto quality : measuredQualities) {
            settings.quality = quality;
            engine.setShadowSettings(settings);
            tick(engine, argc > 1 ? 120 : 12);
            const auto debug = engine.shadowDebugInfo();
            require(debug.resolution == static_cast<unsigned>(quality), "Shadow resolution did not apply");
            require(quality == ShadowQuality::off ? !debug.active && debug.casterDraws == 0
                                                  : debug.active && debug.casterDraws > 0,
                    "Shadow Off did not skip work or enabled shadows have no casters");
            double gpu = 0;
            int gpuFrames = 0;
            const int frames = argc > 1 ? 360 : 25;
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < frames; ++i) {
                tick(engine);
                if (auto value = engine.performance().gpuMilliseconds) {
                    gpu += *value;
                    ++gpuFrames;
                }
            }
            const double elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::cout << "SHADOW resolution=" << static_cast<unsigned>(quality)
                      << " gpu_ms=" << (gpuFrames ? gpu / gpuFrames : -1) << " frame_ms=" << elapsed / frames
                      << " fps=" << frames * 1000 / elapsed << " draws=" << debug.casterDraws << '\n';
            if (argc > 1) {
                save(engine, ("shadows-" + std::to_string(static_cast<unsigned>(quality)) + ".png").c_str());
            }
        }
        const auto sun = engine.directionalLight();
        auto disabled = sun;
        disabled.enabled = false;
        engine.setDirectionalLight(disabled);
        tick(engine, 2);
        require(!engine.shadowDebugInfo().active && !engine.shadowDebugInfo().casterDraws,
                "Disabled sun still rendered shadows");
        engine.setDirectionalLight(sun);
        settings.quality = ShadowQuality::medium;
        engine.setShadowSettings(settings);
        tick(engine, 2);
        const auto before = engine.shadowDebugInfo().casterDraws;
        engine.setShadowProperties(heroes.front(), {false, true});
        tick(engine, 2);
        require(engine.shadowDebugInfo().casterDraws < before, "Per-instance shadow casting was ignored");
        int count = 0;
        auto** windows = SDL_GetWindows(&count);
        require(windows && count == 1, "Shadow window missing");
        auto* window = windows[0];
        SDL_free(windows);
        require(SDL_SetWindowSize(window, 1280, 720), "Shadow resize failed");
        tick(engine, 5);
        for (auto samples : engine.supportedMsaaSamples()) {
            engine.setMsaaSamples(samples);
            tick(engine, 4);
            require(hud.msaaSamples() == samples, "Shadows broke UI MSAA");
        }
        for (auto instance : heroes) {
            engine.removeInstance(instance);
        }
        runtime.restart(engine);
        tick(engine, 3);
        runtime.unload(engine);
        tick(engine, 2);
        require(engine.performance().instances == 0, "Shadow level restart/unload leaked instances");
        std::cout << "PASS shadow qualities, animated casters, ghost policy, per-object flags, stable projection, "
                     "resize, MSAA and lifecycle.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL shadows: " << error.what() << '\n';
        return 1;
    }
}
