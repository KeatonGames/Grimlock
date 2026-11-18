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
            control(engine, runtime, window, "Main Menu", "PLAY");
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
            if (std::string_view{panelName} == "Settings") {
                for (const char* removed : {"Texture filtering", "Anisotropy", "Tone mapping"}) {
                    require(ImGui::GetHoveredID() != ImHashStr(removed, 0, panel->ID),
                            "Technical rendering control still appears in Game Options");
                }
            }
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
        require(shipped.levels.size() == 2 && shipped.levels.front().path.filename() == "forest_defense.json",
                "Game must ship Greenhaven and Gravewatch");
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
        {
            std::ofstream legacy{optionsPath};
            legacy << R"({"texture_filtering":"obsolete", "anisotropy":-1, "tone_mapping":{"old":true}})";
        }
        engine.setRenderSettings({TextureFiltering::bilinear, 2, ToneMapping::reinhard});
        GameHud hud{engine, runtime, true, {}, optionsPath};
        tick(engine, runtime, 3);
        require(!engine.scene() && !runtime.level().waves() && !runtime.navigation().enemies(),
                "menu created gameplay state");
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        require(windows && count == 1, "menu window missing");
        const auto window = SDL_GetWindowID(windows[0]);
        SDL_free(windows);
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
        const auto savedGraphics = engine.renderSettings();
        require(savedGraphics.toneMapping == ToneMapping::filmic &&
                    savedGraphics.anisotropy == engine.supportedAnisotropy().back() &&
                    savedGraphics.textureFiltering ==
                        (savedGraphics.anisotropy > 1 ? TextureFiltering::anisotropic : TextureFiltering::trilinear),
                "Game did not use fixed technical rendering defaults");
        const auto savedOptions = contents(optionsPath);
        require(savedOptions.find("texture_filtering") == std::string::npos &&
                    savedOptions.find("anisotropy") == std::string::npos &&
                    savedOptions.find("tone_mapping") == std::string::npos,
                "Game still saved obsolete technical options");
        control(engine, runtime, window, "Settings", "Back");
        control(engine, runtime, window, "Main Menu", "PLAY");
        tick(engine, runtime, 12);
        require(engine.scene() && engine.scene()->name() == "Kingdom World Map",
                "PLAY did not open a rendered world map");
        require(!runtime.level().waves() && !runtime.navigation().enemies(), "World map spawned gameplay entities");
        require(!ImGui::FindWindowByName("Level Selection"), "Obsolete level-selection panel remains");
        control(engine, runtime, window, "World Map Navigation", "BACK");
        tick(engine, runtime, 3);
        require(!engine.scene(), "Back did not release the world map scene");
        control(engine, runtime, window, "Main Menu", "PLAY");
        tick(engine, runtime, 8);
        const auto* gravewatchMarker = ImGui::FindWindowByName("Map marker##4");
        require(gravewatchMarker && gravewatchMarker->Active, "Gravewatch map marker missing");
        click(engine, runtime, window, {gravewatchMarker->Pos.x + 24, gravewatchMarker->Pos.y + 24});
        tick(engine, runtime, 8);
        const auto* lockedDetails = ImGui::FindWindowByName("Map location details");
        require(lockedDetails && lockedDetails->Active, "Locked chapter cannot be inspected");
        click(engine, runtime, window, {lockedDetails->Pos.x + 156, lockedDetails->Pos.y + lockedDetails->Size.y - 32});
        tick(engine, runtime, 4);
        require(!runtime.level().waves(), "Locked Gravewatch launched without Greenhaven completion");
        const auto mapRole = engine.scene()->object(2).role;
        engine.scene()->setRole(2, "grimlock.map_location:locked:" + shipped.levels.front().id);
        tick(engine, runtime, 3);
        const auto* lockedMarker = ImGui::FindWindowByName("Map marker##2");
        click(engine, runtime, window, {lockedMarker->Pos.x + 24, lockedMarker->Pos.y + 24});
        require(!runtime.level().waves(), "Locked map marker launched gameplay");
        engine.scene()->setRole(2, mapRole);
        tick(engine, runtime, 3);
        capture(engine, root.parent_path() / "world_map_before_selection.png");
        const auto* marker = ImGui::FindWindowByName("Map marker##2");
        require(marker && marker->Active, "World map marker is missing");
        click(engine, runtime, window, {marker->Pos.x + 24, marker->Pos.y + 24});
        require(!runtime.level().waves(), "Location click launched gameplay without confirmation");
        require(ImGui::FindWindowByName("Map location details"), "Selected location has no details");
        tick(engine, runtime, 8);
        const auto* details = ImGui::FindWindowByName("Map location details");
        capture(engine, root.parent_path() / "world_map_selected.png");
        require(details && details->Active, "Details panel was deselected before launch");
        click(engine, runtime, window, {details->Pos.x + 156, details->Pos.y + details->Size.y - 32});
        for (int frame = 0; frame < 60; ++frame) {
            SDL_Delay(10);
            tick(engine, runtime, 1);
        }
        require(runtime.level().waves() && runtime.level().waves()->definition().id == shipped.levels.front().id,
                "Map launch did not load the selected level");
        progress.abandonAttempt();
        progress.startAttempt(shipped.levels.front().id);
        (void)progress.finishAttempt(shipped.levels.front().id, true, 30);
        control(engine, runtime, window, "Game Status", "Choose Level");
        tick(engine, runtime, 8);
        gravewatchMarker = ImGui::FindWindowByName("Map marker##4");
        click(engine, runtime, window, {gravewatchMarker->Pos.x + 24, gravewatchMarker->Pos.y + 24});
        tick(engine, runtime, 8);
        details = ImGui::FindWindowByName("Map location details");
        click(engine, runtime, window, {details->Pos.x + 156, details->Pos.y + details->Size.y - 32});
        for (int frame = 0; frame < 60; ++frame) {
            SDL_Delay(10);
            tick(engine, runtime, 1);
        }
        require(runtime.level().waves() && runtime.level().waves()->definition().id == shipped.levels.back().id,
                "Unlocked Gravewatch marker did not launch the correct chapter");
        runtime.unload(engine);
        std::filesystem::remove_all(folder);
        std::cout << "PASS world map scene, no hero, explicit selection/launch and existing level discovery.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL world map: " << e.what() << '\n';
        return 1;
    }
}