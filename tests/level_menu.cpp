#include "runtime/level_catalog.hpp"
#include "ui/game_hud.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <fstream>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

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

void capture(Engine& engine, const std::filesystem::path& file) {
    const auto frame = engine.captureFrame();
    require(stbi_write_png(file.string().c_str(), frame.width, frame.height, 4, frame.rgba.data(), frame.width * 4) !=
                0,
            "Menu screenshot capture failed");
}

void key(Engine& engine, LevelRuntime& runtime, SDL_WindowID window, SDL_Keycode code, SDL_Scancode scan) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = window;
    event.key.key = code;
    event.key.scancode = scan;
    require(SDL_PushEvent(&event), SDL_GetError());
    tick(engine, runtime, 1);
    event.type = SDL_EVENT_KEY_UP;
    require(SDL_PushEvent(&event), SDL_GetError());
    tick(engine, runtime, 2);
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
    if (std::string_view{panelName} == "Game Status" &&
        (std::string_view{label} == "Restart Level" || std::string_view{label} == "Choose Level")) {
        for (int step = 0; step < 3 && !runtime.paused(); ++step) {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.windowID = window;
            event.key.key = SDLK_ESCAPE;
            event.key.scancode = SDL_SCANCODE_ESCAPE;
            require(SDL_PushEvent(&event), SDL_GetError());
            tick(engine, runtime, 1);
            event.type = SDL_EVENT_KEY_UP;
            require(SDL_PushEvent(&event), SDL_GetError());
            tick(engine, runtime, 1);
        }
        if (std::string_view{label} == "Choose Level") {
            control(engine, runtime, window, "Pause Menu", "Main Menu");
            control(engine, runtime, window, "Main Menu", "Choose Level");
        } else {
            control(engine, runtime, window, "Pause Menu", label);
        }
        return;
    }
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
    const auto seed = std::string_view{label} == "Pause" ? ImHashStr("Status metrics", 0, panel->ID)
                      : scope.empty()                    ? panel->ID
                                                         : ImHashStr(scope.generic_string().c_str(), 0, panel->ID);
    const auto id = ImHashStr(label, 0, seed);
    mouse(window, SDL_EVENT_MOUSE_MOTION, {1, 1});
    tick(engine, runtime);
    for (float y = panel->InnerClipRect.Min.y + 3; y < panel->InnerClipRect.Max.y; y += scope.empty() ? 3 : 10) {
        const bool secondColumn = std::string_view{label} == "Refresh";
        const float x = std::string_view{label} == "Pause" ? panel->InnerClipRect.Max.x - 35
                                                           : panel->DC.CursorStartPos.x + (secondColumn ? 200 : 50);
        for (float scanX = x; scanX < panel->InnerClipRect.Max.x; scanX += 120) {
            const glm::vec2 point{scanX, y};
            mouse(window, SDL_EVENT_MOUSE_MOTION, point);
            tick(engine, runtime, 1);
            if (ImGui::GetHoveredID() == id) {
                click(engine, runtime, window, point);
                return;
            }
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
        const auto shipped = discoverLevels(root);
        const auto levelOneBefore = contents(root / "levels/forest_defense.json");
        require(shipped.levels.size() == 1 && shipped.levels.front().path.filename() == "forest_defense.json",
                "Game must ship exactly Level 1");
        require(std::filesystem::is_regular_file(root / "ui/menu_background.png") &&
                    std::filesystem::is_regular_file(root / "previews/forest_defense.png"),
                "Menu captures missing");
        std::filesystem::create_directories(folder);
        const auto a = folder / "a.json", b = folder / "b.json", bad = folder / "bad.json";
        auto first = loadLevelDefinition(root / "test_fixtures/levels/first_watch.json");
        first.name = "A menu fixture";
        first.id = createLevelId();
        first.description = "Handcrafted menu description";
        first.startingGold = 123;
        saveLevelDefinition(a, first);
        auto second = loadLevelDefinition(root / "test_fixtures/levels/boss_watch.json");
        second.name = "B menu fixture";
        second.id = createLevelId();
        second.startingGold = 99;
        second.waves = {{"Boss encounter", {second.waves.back().groups.back()}, 0}};
        second.waves.front().groups.front().start = 0;
        saveLevelDefinition(b, second);
        const auto duplicate = folder / "duplicate.json";
        saveLevelDefinition(duplicate, second);
        const auto ambiguous = discoverLevels(root);
        require(std::none_of(ambiguous.levels.begin(),
                             ambiguous.levels.end(),
                             [&](const AvailableLevel& entry) {
                                 return entry.id == second.id;
                             }),
                "duplicate IDs mixed independent level records");
        std::filesystem::remove(duplicate);
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
        const auto progressPath = root.parent_path() / "menu-progress-check.json";
        std::filesystem::remove(progressPath);
        PlayerProgress progress{progressPath};
        runtime.attachProgress(progress);
        const auto optionsPath = root.parent_path() / "menu-options-check.json";
        std::filesystem::remove(optionsPath);
        GameHud hud{engine, runtime, true, {}, optionsPath};
        tick(engine, runtime, 3);
        require(!engine.scene() && !runtime.level().waves() && !runtime.navigation().enemies(),
                "menu created gameplay state");
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        require(windows && count == 1, "menu window missing");
        const auto window = SDL_GetWindowID(windows[0]);
        SDL_free(windows);
        control(engine, runtime, window, "Main Menu", "START GAME");
        require(runtime.level().waves() && runtime.level().waves()->definition().name == "Level 01 - Forest Defense",
                "Start Game did not launch Level 1 directly");
        control(engine, runtime, window, "Game Status", "Choose Level");
        control(engine, runtime, window, "Level Selection", "Main Menu");
        control(engine, runtime, window, "Main Menu", "Settings");
        capture(engine, root.parent_path() / "phase48_options.png");
        control(engine, runtime, window, "Settings", "Display mode");
        key(engine, runtime, window, SDLK_END, SDL_SCANCODE_END);
        key(engine, runtime, window, SDLK_RETURN, SDL_SCANCODE_RETURN);
        control(engine, runtime, window, "Settings", "APPLY");
        require(SDL_GetWindowFlags(SDL_GetWindowFromID(window)) & SDL_WINDOW_FULLSCREEN,
                "Fullscreen setting was not applied");
        control(engine, runtime, window, "Settings", "Display mode");
        key(engine, runtime, window, SDLK_HOME, SDL_SCANCODE_HOME);
        key(engine, runtime, window, SDLK_RETURN, SDL_SCANCODE_RETURN);
        control(engine, runtime, window, "Settings", "APPLY");
        require(!(SDL_GetWindowFlags(SDL_GetWindowFromID(window)) & SDL_WINDOW_FULLSCREEN),
                "Windowed mode did not restore");
        control(engine, runtime, window, "Settings", "VSync");
        control(engine, runtime, window, "Settings", "APPLY");
        require(!engine.vsyncEnabled() && std::filesystem::is_regular_file(optionsPath),
                "VSync/options were not applied and saved");
        control(engine, runtime, window, "Settings", "VSync");
        control(engine, runtime, window, "Settings", "APPLY");
        require(engine.vsyncEnabled(), "VSync did not restore");
        control(engine, runtime, window, "Settings", "MSAA");
        key(engine, runtime, window, SDLK_END, SDL_SCANCODE_END);
        key(engine, runtime, window, SDLK_RETURN, SDL_SCANCODE_RETURN);
        control(engine, runtime, window, "Settings", "APPLY");
        const auto savedMsaa = engine.msaaSamples();
        require(savedMsaa == engine.supportedMsaaSamples().back(), "MSAA option did not apply the supported mode");
        control(engine, runtime, window, "Settings", "Shadows");
        key(engine, runtime, window, SDLK_END, SDL_SCANCODE_END);
        key(engine, runtime, window, SDLK_RETURN, SDL_SCANCODE_RETURN);
        control(engine, runtime, window, "Settings", "APPLY");
        const auto savedShadow = engine.shadowSettings().quality;
        require(savedShadow == engine.supportedShadowQualities().back(), "Shadow quality option did not apply");
        control(engine, runtime, window, "Settings", "Back");
        control(engine, runtime, window, "Main Menu", "Choose Level");
        capture(engine, root.parent_path() / "phase48_levels.png");
        control(engine, runtime, window, "Level Selection", first.name.c_str(), a);
        const auto authored = contents(a);
        control(engine, runtime, window, "Level Selection", "Play");
        require(runtime.level().economy().gold() == 123 && runtime.heroes()->heroes().size() == 3 &&
                    engine.scene()->navigationGrid() && !runtime.navigation().placement().zones().empty(),
                "selected level was incompletely initialized");
        runtime.update(engine, 1.25);
        require(std::abs(runtime.completionSeconds() - 1.25) < 1e-8 && progress.find(first.id)->attempts == 1,
                "active preparation time or attempt count failed");
        runtime.startWave();
        runtime.update(engine, 0.2);
        require(!runtime.navigation().enemies()->enemies().empty(), "selected level did not spawn its wave");
        control(engine, runtime, window, "Game Status", "Restart Level");
        require(runtime.level().economy().gold() == 123 && runtime.navigation().enemies()->enemies().empty(),
                "restart leaked state");
        require(progress.find(first.id)->attempts == 2 && progress.find(first.id)->victories == 0 &&
                    runtime.completionSeconds() == 0,
                "restart did not start an independent attempt");
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
        const auto cube = engine.loadModel(root / "towers/structures/primitives/cube.glb");
        control(engine, runtime, window, "Game Status", "Choose Level");
        require(!engine.scene() && !runtime.heroes() && !runtime.level().waves() && !runtime.navigation().enemies(),
                "return to selection retained gameplay state");
        control(engine, runtime, window, "Level Selection", second.name.c_str(), b);
        control(engine, runtime, window, "Level Selection", "Play");
        require(runtime.level().economy().gold() == 99 && !engine.scene()->navigationGrid() &&
                    runtime.level().combat()->shotsFired() == 0 && runtime.navigation().towers().towers().empty() &&
                    engine.loadModel(root / "towers/structures/primitives/cube.glb") == cube,
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
        require(progress.find(second.id)->victories == 1 && runtime.personalRecord(), "victory was not recorded");
        const auto completion = runtime.completionSeconds();
        runtime.update(engine, 5);
        require(runtime.completionSeconds() == completion && progress.find(second.id)->victories == 1,
                "result screen time or duplicate updates changed the record");
        const auto ordering = discoverLevels(root);
        const auto current =
            std::find_if(ordering.levels.begin(), ordering.levels.end(), [&](const AvailableLevel& entry) {
                return entry.id == second.id;
            });
        require(current != ordering.levels.end() && std::next(current) != ordering.levels.end(),
                "next level fixture missing");
        const auto nextId = std::next(current)->id;
        control(engine, runtime, window, "Level Result", "Next Level");
        require(runtime.level().waves()->definition().id == nextId && progress.lastPlayed() == nextId,
                "Next Level did not follow the current ordering");
        control(engine, runtime, window, "Game Status", "Choose Level");
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
        invalid.id = createLevelId();
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
        require(contents(root / "levels/forest_defense.json") == levelOneBefore,
                "Menus changed Level 1 gameplay metadata");
        engine.detachOverlay();
        engine.setVsync(false);
        std::filesystem::remove_all(folder);
        {
            GameHud reopenedHud{engine, runtime, true, {}, optionsPath};
            tick(engine, runtime, 2);
            require(engine.vsyncEnabled() && engine.msaaSamples() == savedMsaa &&
                        engine.shadowSettings().quality == savedShadow,
                    "Saved display/MSAA/shadow preferences did not reload");
            control(engine, runtime, window, "Main Menu", "Choose Level");
            for (int frame = 0; frame < 20; ++frame) {
                tick(engine, runtime, 1);
                SDL_Delay(10);
            }
            capture(engine, root.parent_path() / "phase48_levels.png");
        }
        PlayerProgress reopened{progressPath};
        require(reopened.error().empty() && reopened.find(second.id)->completed &&
                    reopened.find(second.id)->victories == 1,
                "gameplay progress did not survive reopening");
        std::filesystem::remove(progressPath);
        std::filesystem::remove(optionsPath);
        std::filesystem::remove_all(folder);
        std::cout << "PASS discovery, descriptions, live edits, menus, complete loading, switching, restart and "
                     "failed-load cleanup.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL level menu: " << error.what() << '\n';
        return 1;
    }
}
