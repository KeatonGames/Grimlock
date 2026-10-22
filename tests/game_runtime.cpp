#include "runtime/level_runtime.hpp"
#include "ui/game_hud.hpp"

#include <SDL3/SDL.h>
#include <fstream>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>

using namespace grimlock;
using namespace grimlock::game;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::string contents(const std::filesystem::path& path) {
    std::ifstream file{path};
    return {std::istreambuf_iterator<char>{file}, {}};
}

void tick(Engine& engine, LevelRuntime& runtime, int frames = 2) {
    for (int i = 0; i < frames; ++i) {
        require(engine.update([&](double) {
            runtime.update(engine, 0);
        }),
                "game window closed");
    }
}

void mouse(SDL_WindowID window, Uint32 type, glm::vec2 point) {
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.windowID = window;
        event.motion.x = point.x;
        event.motion.y = point.y;
    } else {
        event.button.windowID = window;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = point.x;
        event.button.y = point.y;
    }
    require(SDL_PushEvent(&event), SDL_GetError());
}

void click(Engine& engine, LevelRuntime& runtime, SDL_WindowID window, glm::vec2 point) {
    mouse(window, SDL_EVENT_MOUSE_MOTION, point);
    tick(engine, runtime);
    mouse(window, SDL_EVENT_MOUSE_BUTTON_DOWN, point);
    tick(engine, runtime);
    mouse(window, SDL_EVENT_MOUSE_BUTTON_UP, point);
    tick(engine, runtime);
}

void control(Engine& engine,
             LevelRuntime& runtime,
             SDL_WindowID window,
             const char* panelName,
             const char* label,
             const char* scope = nullptr) {
    auto* panel = ImGui::FindWindowByName(panelName);
    require(panel && panel->Active, "game HUD panel missing");
    const auto seed = scope ? ImHashStr(scope, 0, panel->ID) : panel->ID;
    const auto id = ImHashStr(label, 0, seed);
    for (float y = panel->InnerClipRect.Min.y + 4; y < panel->InnerClipRect.Max.y; y += 4) {
        const glm::vec2 point{panel->DC.CursorStartPos.x + 50, y};
        mouse(window, SDL_EVENT_MOUSE_MOTION, point);
        tick(engine, runtime, 1);
        if (ImGui::GetHoveredID() == id) {
            click(engine, runtime, window, point);
            return;
        }
    }
    throw std::runtime_error(label);
}

glm::vec2 screen(Engine& engine, SDL_Window* window, glm::vec3 point) {
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    const auto& camera = engine.scene()->resolvedCamera();
    const auto clip = camera.projection(static_cast<float>(width) / height) * camera.view() * glm::vec4{point, 1};
    return {(clip.x / clip.w + 1) * width * 0.5F, (clip.y / clip.w + 1) * height * 0.5F};
}

void finishBattle(Engine& engine, LevelRuntime& runtime) {
    for (int frame = 0; frame < 2400 && runtime.level().waves()->phase() == LevelPhase::battle; ++frame) {
        runtime.update(engine, 1.0 / 30);
        for (const auto& enemy : runtime.navigation().enemies()->enemies()) {
            require(enemy.navigation->contains(enemy.position), "game enemy entered an obstacle");
        }
        if (frame % 120 == 0) {
            tick(engine, runtime, 1);
        }
    }
    tick(engine, runtime);
}
} // namespace

int main() {
    try {
        Engine engine{EngineConfig{.windowTitle = "Game runtime checks"}};
        const auto assetRoot = engine.assetRoot();
        auto definition = loadLevelDefinition(assetRoot / "levels/first_watch.json");
        auto scene = Scene::load(assetRoot / definition.scene);
        scene->setName("Editor-authored runtime fixture");
        auto zone = PlacementSystem{};
        zone.synchronize(scene.get());
        require(!zone.zones().empty(), "build zones missing from authored level");
        auto edited = zone.zones().front();
        edited.name = "Edited zone";
        PlacementSystem::editZone(*scene, edited);
        definition.scene = "scenes/game-runtime-edited.json";
        scene->save(assetRoot / definition.scene);
        definition.name = "Edited level";
        definition.startingGold = 120;
        definition.waves.resize(1);
        definition.waves[0].goldReward = 7;
        definition.waves[0].groups[0].count = 2;
        definition.waves[0].groups[0].batchSize = 1;
        definition.waves[0].groups[0].interval = 0.1;
        const auto levelPath = assetRoot / "levels/game-runtime-edited.json";
        saveLevelDefinition(levelPath, definition);
        const auto sceneBefore = contents(assetRoot / definition.scene), levelBefore = contents(levelPath);
        LevelRuntime runtime;
        runtime.load(engine, levelPath);
        GameHud hud{engine, runtime};
        tick(engine, runtime, 4);
        require(!ImGui::FindWindowByName("Scene Hierarchy") && !ImGui::FindWindowByName("Inspector") &&
                    !(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) && !engine.objectSelectionEnabled(),
                "standalone Game contains editor controls");
        require(runtime.level().economy().gold() == 120 &&
                    runtime.navigation().placement().zones().front().name == "Edited zone",
                "Game did not load edited level/scene data");
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        require(windows && count == 1, "game window missing");
        auto* window = windows[0];
        const auto id = SDL_GetWindowID(window);
        SDL_free(windows);
        SDL_Event focus{};
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        focus.window.windowID = id;
        require(SDL_PushEvent(&focus), SDL_GetError());
        tick(engine, runtime);
        control(engine, runtime, id, "Build Towers", "Basic tower", "basic");
        require(runtime.placing() && runtime.navigation().towers().towers().empty() &&
                    runtime.level().economy().gold() == 120,
                "HUD click placed a tower or charged before confirmation");
        const auto point = screen(engine, window, {-3.3F, 0, 0});
        mouse(id, SDL_EVENT_MOUSE_MOTION, point);
        tick(engine, runtime);
        require(runtime.preview() && runtime.placementError().empty(), "game placement preview is not valid");
        click(engine, runtime, id, point);
        require(runtime.navigation().towers().towers().size() == 1 && runtime.level().economy().gold() == 90 &&
                    !runtime.navigation().navigation().contains({-3.3, 0, 0}),
                "game construction or navigation update failed");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        tick(engine, runtime);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        tick(engine, runtime);
        require(!runtime.placing(), "Escape did not cancel gameplay placement");
        control(engine, runtime, id, "Game Status", "Start Wave");
        require(runtime.level().waves()->phase() == LevelPhase::battle, "HUD did not start the wave");
        finishBattle(engine, runtime);
        const auto* resultPanel = ImGui::FindWindowByName("Level Result");
        require(runtime.level().waves()->phase() == LevelPhase::victory && runtime.level().waves()->health() == 28 &&
                    runtime.level().economy().gold() == 97 && resultPanel && resultPanel->Active,
                "Game victory, reward or result HUD failed");
        require(contents(levelPath) == levelBefore && contents(assetRoot / definition.scene) == sceneBefore,
                "gameplay overwrote authored files");
        control(engine, runtime, id, "Level Result", "Play Again");
        require(runtime.level().economy().gold() == 120 && runtime.level().waves()->health() == 30 &&
                    runtime.navigation().towers().towers().empty() &&
                    runtime.navigation().enemies()->enemies().empty() &&
                    runtime.navigation().navigation().contains({-3.3, 0, 0}),
                "game restart retained runtime state");
        definition.objectiveHealth = 1;
        definition.startingGold = 20;
        saveLevelDefinition(levelPath, definition);
        runtime.restart(engine);
        bool unaffordable = false;
        try {
            runtime.beginPlacement("basic");
        } catch (const std::invalid_argument&) {
            unaffordable = true;
        }
        require(unaffordable && !runtime.placing(), "Game bypassed construction costs");
        tick(engine, runtime);
        control(engine, runtime, id, "Game Status", "Start Wave");
        finishBattle(engine, runtime);
        require(runtime.level().waves()->phase() == LevelPhase::defeat && runtime.level().economy().gold() == 20,
                "Game defeat or reward handling incorrect");
        std::cout << "PASS independent Game HUD, edited level loading, placement/economy/navigation, waves, "
                     "victory/defeat, restart and read-only authoring files.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Game runtime: " << error.what() << '\n';
        return 1;
    }
}
