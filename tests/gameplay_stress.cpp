#include "allocation_counter.hpp"
#include "ui/game_hud.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <imgui.h>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

using namespace grimlock;
using namespace grimlock::game;

namespace {
void measure(Engine& engine, LevelRuntime& runtime, SDL_Window* window, const char* name, int samples, bool world) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = SDL_GetWindowID(window);
    event.motion.x = world ? 480 : 35;
    event.motion.y = world ? 220 : 35;
    SDL_WarpMouseInWindow(window, event.motion.x, event.motion.y);
    SDL_PushEvent(&event);
    std::vector<double> frames;
    frames.reserve(samples);
    double game = 0, ui = 0, preparation = 0, render = 0, gpu = 0, animation = 0;
    std::size_t gpuSamples = 0;
    std::uint64_t allocations = 0;
    for (int frame = -30; frame < samples; ++frame) {
        SDL_Event focus{};
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        focus.window.windowID = SDL_GetWindowID(window);
        SDL_PushEvent(&focus);
        SDL_PushEvent(&event);
        checks::AllocationScope scope;
        if (!engine.update([&](double) {
                runtime.update(engine, 1.0 / 60);
            })) {
            throw std::runtime_error("Gameplay stress window closed");
        }
        const auto allocated = scope.stop();
        if (frame < 0) {
            continue;
        }
        const auto& timing = engine.performance();
        frames.push_back(timing.frameMilliseconds);
        game += timing.gameMilliseconds;
        preparation += timing.preparationMilliseconds;
        animation += timing.animationMilliseconds;
        render += timing.renderMilliseconds;
        ui += timing.frameMilliseconds - timing.gameMilliseconds - timing.preparationMilliseconds -
              timing.renderMilliseconds;
        allocations += allocated.count;
        if (timing.gpuMilliseconds) {
            gpu += *timing.gpuMilliseconds;
            ++gpuSamples;
        }
    }
    double privateMb = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                             sizeof(memory))) {
        privateMb = memory.PrivateUsage / (1024.0 * 1024);
    }
#endif
    const auto mean = std::accumulate(frames.begin(), frames.end(), 0.0) / samples;
    if (runtime.level().waves() && (!engine.input().focused || ImGui::GetIO().WantCaptureMouse == world)) {
        throw std::runtime_error("Gameplay stress pointer did not exercise its requested UI/world path");
    }
    std::sort(frames.begin(), frames.end());
    const auto percentile = [&](double fraction) {
        return frames[static_cast<std::size_t>((frames.size() - 1) * fraction)];
    };
    std::cout << std::fixed << std::setprecision(3) << "GAMEPLAY," << name << ',' << 1000 / mean << ',' << mean << ','
              << percentile(0.95) << ',' << percentile(0.99) << ',' << frames.back() << ',' << game / samples << ','
              << ui / samples << ',' << preparation / samples << ',' << animation / samples << ',' << render / samples
              << ',' << (gpuSamples ? gpu / gpuSamples : -1) << ',' << privateMb << ',' << allocations << ','
              << engine.performance().instances << ',' << engine.performance().models << std::endl;
}
} // namespace

int main(int argc, char** argv) {
    try {
        const int samples = argc > 1 ? std::stoi(argv[1]) : 180;
        const bool profile = argc == 3 && std::string_view{argv[2]} == "--profile";
        if (samples < 1 || samples > 3600 || argc > 3 || (argc == 3 && !profile)) {
            throw std::invalid_argument("Usage: GameplayStressChecks [samples: 1..3600] [--profile]");
        }
        Engine engine{EngineConfig{.windowTitle = "Gameplay stress", .width = 960, .height = 540}};
        engine.setPerformanceEnabled(true);
        LevelRuntime runtime;
        GameHud hud{engine, runtime};
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        if (!windows || count != 1) {
            throw std::runtime_error("Missing gameplay stress window");
        }
        auto* window = windows[0];
        SDL_free(windows);
        SDL_RaiseWindow(window);
        std::cout << "scenario,name,fps,mean_ms,p95_ms,p99_ms,max_ms,game_ms,ui_events_ms,preparation_ms,animation_ms,"
                     "render_ms,gpu_ms,private_mb,allocations,instances,models\n";
        measure(engine, runtime, window, "empty", samples, false);
        if (!profile) {
            SDL_Delay(300);
            if (!engine.update([&](double delta) {
                    if (delta > 0.25) {
                        throw std::runtime_error("A stalled frame exceeded the simulation recovery budget");
                    }
                })) {
                throw std::runtime_error("Window closed during stall recovery");
            }
        }
        for (int pass = 0; pass < 3; ++pass) {
            runtime.load(engine, "assets/levels/first_watch.json");
            for (std::size_t type = 1; type < runtime.heroes()->typeCount(); ++type) {
                const glm::dvec3 point{6, 0, type == 1 ? -2 : 2};
                (void)runtime.heroes()->deploy(point, {}, type);
            }
            for (const auto* type : {"basic", "wide"}) {
                auto tower = runtime.navigation().towers().definition(type).instance();
                tower.position = {0, tower.halfExtent.y, tower.type == "basic" ? 2.5 : -2.5};
                (void)runtime.level().buildTower(engine, runtime.navigation(), tower);
            }
            if (pass == 0) {
                const auto start = std::chrono::steady_clock::now();
                const Ray ray{{0, 30, 0}, {0, -1, 0}, 0.01, 100};
                for (int query = 0; query < 100; ++query) {
                    (void)engine.pickObject(ray);
                }
                std::cout
                    << "PICK_MISS_MS,"
                    << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 100
                    << std::endl;
            }
            measure(engine, runtime, window, "preparation-ui", samples, false);
            measure(engine, runtime, window, "preparation-world", samples, true);
            runtime.startWave();
            measure(engine, runtime, window, "battle-world", samples, true);
            runtime.unload(engine);
            if (!engine.update() || engine.performance().instances != 0) {
                throw std::runtime_error("Level unload retained runtime instances");
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Gameplay stress: " << error.what() << '\n';
        return 1;
    }
}
