#include "runtime/level_catalog.hpp"
#include "ui/game_hud.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
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

void tick(Engine& engine, LevelRuntime& runtime, int count = 2) {
    for (int i = 0; i < count; ++i) {
        require(engine.update([&](double) {
            runtime.update(engine, 0);
        }),
                "menu window closed");
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
             const std::filesystem::path& scope = {}) {
    auto* panel = ImGui::FindWindowByName(panelName);
    require(panel && panel->Active, "menu panel missing");
    if (!scope.empty()) {
        for (auto* child : ImGui::GetCurrentContext()->Windows) {
            if (child->Active && child->ParentWindow == panel) {
                panel = child;
                break;
            }
        }
    }
    const auto seed = scope.empty() ? panel->ID : ImHashStr(scope.generic_string().c_str(), 0, panel->ID);
    const auto id = ImHashStr(label, 0, seed);
    mouse(window, SDL_EVENT_MOUSE_MOTION, {1, 1});
    tick(engine, runtime);
    for (float y = panel->InnerClipRect.Min.y + 3; y < panel->InnerClipRect.Max.y; y += 3) {
        const bool secondColumn =
            std::string_view{label} == "Refresh" ||
            (std::string_view{panelName} == "Game Status" && std::string_view{label} == "Choose Level");
        const glm::vec2 point{panel->DC.CursorStartPos.x + (secondColumn ? 200 : 50), y};
        mouse(window, SDL_EVENT_MOUSE_MOTION, point);
        tick(engine, runtime, 1);
        if (ImGui::GetHoveredID() == id) {
            click(engine, runtime, window, point);
            return;
        }
    }
    throw std::runtime_error(std::string{"Missing menu control: "} + label);
}

std::string contents(const std::filesystem::path& path) {
    std::ifstream input{path};
    return {std::istreambuf_iterator<char>{input}, {}};
}
} // namespace

int main() {
    try {
        Engine engine{EngineConfig{.windowTitle = "Level menu checks"}};
        const auto root = engine.assetRoot();
        const auto folder = root / "levels/menu_checks";
        std::filesystem::remove_all(folder);
        std::filesystem::create_directories(folder);
        const auto a = folder / "a.json", b = folder / "b.json", bad = folder / "bad.json";
        auto first = loadLevelDefinition(root / "levels/first_watch.json");
        first.name = "A menu fixture";
        first.description = "Handcrafted menu description";
        first.startingGold = 123;
        saveLevelDefinition(a, first);
        auto second = loadLevelDefinition(root / "levels/boss_watch.json");
        second.name = "B menu fixture";
        second.startingGold = 99;
        second.waves = {{"Boss encounter", {second.waves.back().groups.back()}, 0}};
        second.waves.front().groups.front().start = 0;
        saveLevelDefinition(b, second);
        std::ofstream{bad} << "{";
        auto incomplete = first;
        incomplete.scene = "scenes/missing-menu-scene.json";
        saveLevelDefinition(folder / "missing.json", incomplete);
        const auto catalog = discoverLevels(root);
        const auto find = [&](const std::filesystem::path& path) {
            return std::find_if(catalog.levels.begin(), catalog.levels.end(), [&](const AvailableLevel& entry) {
                return entry.path == path;
            });
        };
        require(find(a) != catalog.levels.end() && find(b) != catalog.levels.end() &&
                    find(a)->description == first.description && catalog.errors.size() >= 2,
                "valid levels, descriptions or graceful invalid-file discovery failed");
        require(!discoverLevels(root / "missing-assets").errors.empty(), "missing asset directory was accepted");
        LevelRuntime runtime;
        GameHud hud{engine, runtime, true};
        tick(engine, runtime, 3);
        require(!engine.scene() && !runtime.level().waves() && !runtime.navigation().enemies(),
                "menu created gameplay state");
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        require(windows && count == 1, "menu window missing");
        const auto window = SDL_GetWindowID(windows[0]);
        SDL_free(windows);
        control(engine, runtime, window, "Main Menu", "Choose Level");
        control(engine, runtime, window, "Level Selection", first.name.c_str(), a);
        const auto authored = contents(a);
        control(engine, runtime, window, "Level Selection", "Play");
        require(runtime.level().economy().gold() == 123 &&
                    runtime.heroes()->heroes().size() == first.heroSpawns.size() && engine.scene()->navigationGrid() &&
                    !runtime.navigation().placement().zones().empty(),
                "selected level was incompletely initialized");
        runtime.startWave();
        runtime.update(engine, 0.2);
        require(!runtime.navigation().enemies()->enemies().empty(), "selected level did not spawn its wave");
        control(engine, runtime, window, "Game Status", "Restart Level");
        require(runtime.level().economy().gold() == 123 && runtime.navigation().enemies()->enemies().empty(),
                "restart leaked state");
        auto tower = runtime.navigation().towers().definition("basic").instance();
        tower.position = {4, tower.halfExtent.y, -2.5};
        (void)runtime.navigation().towers().create(engine, tower);
        runtime.navigation().synchronize(engine);
        require(runtime.navigation().spawnAt(engine, 0, {{2.8, -2.5}, {2.8, -2.5}, 0}, 1) == 1,
                "switching combat fixture spawn failed");
        runtime.startWave();
        runtime.update(engine, 0.001);
        require(runtime.level().combat()->shotsFired() > 0 &&
                    !runtime.level().combat()->projectiles().projectiles().empty(),
                "switching fixture did not leave active projectiles");
        const auto cube = engine.loadModel(root / "models/cube.obj");
        control(engine, runtime, window, "Game Status", "Choose Level");
        require(!engine.scene() && !runtime.heroes() && !runtime.level().waves() && !runtime.navigation().enemies(),
                "return to selection retained gameplay state");
        control(engine, runtime, window, "Level Selection", second.name.c_str(), b);
        control(engine, runtime, window, "Level Selection", "Play");
        require(runtime.level().economy().gold() == 99 && !engine.scene()->navigationGrid() &&
                    runtime.level().combat()->shotsFired() == 0 && runtime.navigation().towers().towers().empty() &&
                    engine.loadModel(root / "models/cube.obj") == cube,
                "switching level leaked state or lost terrain");
        runtime.startWave();
        runtime.update(engine, 0);
        auto& enemies = *runtime.navigation().enemies();
        require(enemies.enemies().size() == 1 && enemies.definition(enemies.enemies().front().type).boss,
                "boss wave lost on load");
        const auto boss = enemies.enemies().front().id;
        require(enemies.applyDamage(boss, enemies.find(boss)->health), "boss death fixture failed");
        runtime.update(engine, 0);
        tick(engine, runtime);
        require(runtime.level().waves()->phase() == LevelPhase::victory, "menu-loaded level did not complete");
        control(engine, runtime, window, "Level Result", "Choose Level");
        first.startingGold = 321;
        saveLevelDefinition(a, first);
        control(engine, runtime, window, "Level Selection", "Refresh");
        control(engine, runtime, window, "Level Selection", first.name.c_str(), a);
        control(engine, runtime, window, "Level Selection", "Play");
        require(runtime.level().economy().gold() == 321, "edited level was not reread");
        require(authored != contents(a), "fixture edit was not saved");
        const auto edited = contents(a);
        control(engine, runtime, window, "Game Status", "Choose Level");
        auto invalid = second;
        invalid.name = "C unreachable fixture";
        invalid.objectivePosition = {100000, 100000};
        const auto c = folder / "c.json";
        saveLevelDefinition(c, invalid);
        const auto discoveryDeadline = SDL_GetTicks() + 2300;
        while (SDL_GetTicks() < discoveryDeadline) {
            tick(engine, runtime, 1);
            SDL_Delay(10);
        }
        control(engine, runtime, window, "Level Selection", invalid.name.c_str(), c);
        control(engine, runtime, window, "Level Selection", "Play");
        require(!engine.scene() && !runtime.level().waves() && ImGui::FindWindowByName("Level Selection")->Active,
                "failed navigation load left partial gameplay state");
        require(contents(a) == edited, "Game modified an authored level");
        control(engine, runtime, window, "Level Selection", "Main Menu");
        require(ImGui::FindWindowByName("Main Menu")->Active, "return to main menu failed");
        control(engine, runtime, window, "Main Menu", "Quit");
        require(hud.quitRequested(), "quit request failed");
        std::filesystem::remove_all(folder);
        std::cout << "PASS discovery, descriptions, live edits, menus, complete loading, switching, restart and "
                     "failed-load cleanup.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL level menu: " << error.what() << '\n';
        return 1;
    }
}
