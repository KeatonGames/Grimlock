#include "ui/game_hud.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
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
            runtime.update(engine, 1.0 / 60);
        }),
                "UI window closed");
    }
}

void move(SDL_WindowID window, ImVec2 p) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = window;
    event.motion.x = p.x;
    event.motion.y = p.y;
    require(SDL_PushEvent(&event), SDL_GetError());
}

void click(Engine& engine, LevelRuntime& runtime, SDL_WindowID window, ImVec2 p) {
    move(window, p);
    tick(engine, runtime);
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID = window;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = p.x;
    event.button.y = p.y;
    require(SDL_PushEvent(&event), SDL_GetError());
    tick(engine, runtime);
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    require(SDL_PushEvent(&event), SDL_GetError());
    tick(engine, runtime);
}

void escape(Engine& engine, LevelRuntime& runtime, SDL_WindowID window);

void pause(Engine& engine, LevelRuntime& runtime, SDL_WindowID window) {
    for (int step = 0; step < 3 && !runtime.paused(); ++step) {
        escape(engine, runtime, window);
    }
    require(runtime.paused(), "Escape did not open pause menu");
}

void control(Engine& engine, LevelRuntime& runtime, SDL_WindowID window, const char* panelName, const char* label) {
    auto* panel = ImGui::FindWindowByName(panelName);
    require(panel && panel->Active, "polished UI panel missing");
    const auto seed = std::string_view{label} == "Pause" ? ImHashStr("Status metrics", 0, panel->ID) : panel->ID;
    const auto id = ImHashStr(label, 0, seed);
    const float x =
        std::string_view{label} == "Pause" ? panel->InnerClipRect.Max.x - 35 : panel->DC.CursorStartPos.x + 50;
    move(window, {1, 1});
    tick(engine, runtime);
    for (float y = panel->InnerClipRect.Min.y + 3; y < panel->InnerClipRect.Max.y; y += 3) {
        for (float scanX = x; scanX < panel->InnerClipRect.Max.x; scanX += 120) {
            move(window, {scanX, y});
            tick(engine, runtime, 1);
            if (ImGui::GetHoveredID() == id) {
                click(engine, runtime, window, {scanX, y});
                return;
            }
        }
    }
    throw std::runtime_error(std::string{"UI control missing: "} + label);
}

void escape(Engine& engine, LevelRuntime& runtime, SDL_WindowID window) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = window;
    event.key.key = SDLK_ESCAPE;
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    require(SDL_PushEvent(&event), SDL_GetError());
    tick(engine, runtime, 1);
    event.type = SDL_EVENT_KEY_UP;
    require(SDL_PushEvent(&event), SDL_GetError());
    tick(engine, runtime);
}

void maximumScale(Engine& engine, LevelRuntime& runtime, SDL_WindowID window) {
    auto* panel = ImGui::FindWindowByName("Settings");
    const auto id = ImHashStr("UI Scale", 0, panel->ID);
    for (float y = panel->InnerClipRect.Min.y + 3; y < panel->InnerClipRect.Max.y; y += 3) {
        move(window, {panel->DC.CursorStartPos.x + 30, y});
        tick(engine, runtime, 1);
        if (ImGui::GetHoveredID() == id) {
            click(engine, runtime, window, {panel->WorkRect.Max.x - 100 * ImGui::GetStyle().FontScaleMain - 3, y});
            require(ImGui::GetStyle().FontScaleMain > 1, "UI scale did not affect real font/layout scaling");
            return;
        }
    }
    throw std::runtime_error("UI scale control missing");
}

bool overlaps(const ImGuiWindow& a, const ImGuiWindow& b) {
    return a.Pos.x < b.Pos.x + b.Size.x - 1 && b.Pos.x < a.Pos.x + a.Size.x - 1 && a.Pos.y < b.Pos.y + b.Size.y - 1 &&
           b.Pos.y < a.Pos.y + a.Size.y - 1;
}

void layout() {
    const auto* v = ImGui::GetMainViewport();
    const char* names[]{"Round", "Objective Health", "Wave Controls", "Build Towers", "Boss Health", "Coins"};
    for (int i = 0; i < 6; ++i) {
        const auto* panel = ImGui::FindWindowByName(names[i]);
        require(panel && panel->Active, "responsive panel missing");
        require(panel->Pos.x >= v->Pos.x && panel->Pos.y >= v->Pos.y &&
                    panel->Pos.x + panel->Size.x <= v->Pos.x + v->Size.x + 1 &&
                    panel->Pos.y + panel->Size.y <= v->Pos.y + v->Size.y + 1,
                "HUD clipped outside viewport");
        for (int j = 0; j < i; ++j) {
            require(!overlaps(*panel, *ImGui::FindWindowByName(names[j])), "HUD panels overlap");
        }
    }
}
} // namespace

int main(int argc, char**) {
    try {
        Engine engine{EngineConfig{.windowTitle = "Game UI visual checks", .width = 1280, .height = 720}};
        engine.setPerformanceEnabled(true);
        PlayerProgress unavailable;
        LevelRuntime runtime;
        runtime.load(engine, "assets/test_fixtures/levels/first_watch.json");
        GameHud hud{engine, runtime};
        tick(engine, runtime, 4);
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        require(windows && count == 1, "UI window missing");
        auto* window = windows[0];
        const auto id = SDL_GetWindowID(window);
        SDL_free(windows);
        const auto frameModel =
            engine.loadModel(engine.assetRoot() / "towers/structures/tower_defense/construction-zone.glb");
        const auto frameInfo = engine.modelInfo(frameModel);
        require(frameInfo.maximum.y - frameInfo.minimum.y <= 0.051F,
                "Construction frame is an upright ribbon instead of matching the low selection corners");
        const auto frameTriangles = engine.modelTriangles(frameModel);
        const auto covered = [&](glm::vec2 point) {
            return std::any_of(frameTriangles.begin(), frameTriangles.end(), [&](const auto& triangle) {
                if (glm::cross(triangle[1] - triangle[0], triangle[2] - triangle[0]).y <= 1e-5F ||
                    triangle[0].y < 0.049F || triangle[1].y < 0.049F || triangle[2].y < 0.049F) {
                    return false;
                }
                const auto side = [&](glm::vec3 a, glm::vec3 b) {
                    return (b.x - a.x) * (point.y - a.z) - (b.z - a.z) * (point.x - a.x);
                };
                const float a = side(triangle[0], triangle[1]), b = side(triangle[1], triangle[2]),
                            c = side(triangle[2], triangle[0]);
                return (a >= -1e-5F && b >= -1e-5F && c >= -1e-5F) || (a <= 1e-5F && b <= 1e-5F && c <= 1e-5F);
            });
        };
        require(covered({0.48F, 0}) && covered({-0.48F, 0}) && covered({0, 0.48F}) && covered({0, -0.48F}) &&
                    !covered({0, 0}),
                "Construction frame is not a complete square with an open center");
        const auto availableZoneInstances = engine.performance().instances;
        const auto buildPoint = runtime.navigation().placement().zones().front();
        const auto tower = buildPoint.snap(runtime.navigation().towers().definition("basic").instance());
        const auto built = runtime.level().buildTower(engine, runtime.navigation(), tower);
        tick(engine, runtime);
        require(engine.performance().instances + 1 == availableZoneInstances,
                "Constructing a tower did not remove its available-zone frame");
        require(runtime.navigation().towers().applyDamage(engine, built, 10000),
                "Build-zone marker death fixture failed");
        tick(engine, runtime);
        require(engine.performance().instances == availableZoneInstances,
                "Destroying a tower did not restore its build-zone frame");
        runtime.restart(engine);
        tick(engine, runtime);
        auto* waveControls = ImGui::FindWindowByName("Gameplay Settings");
        require(waveControls && waveControls->Active, "Gameplay settings panel missing");
        click(engine,
              runtime,
              id,
              {waveControls->Pos.x + waveControls->Size.x - 18, waveControls->Pos.y + waveControls->Size.y - 18});
        require(runtime.paused(), "Gear button did not pause gameplay");
        control(engine, runtime, id, "Pause Menu", "Settings");
        require(ImGui::FindWindowByName("Settings")->Active, "Gear menu did not provide settings access");
        control(engine, runtime, id, "Settings", "Back");
        control(engine, runtime, id, "Pause Menu", "Resume");
        require(!runtime.paused(), "Gear menu did not resume gameplay");
        runtime.beginPlacement("basic");
        escape(engine, runtime, id);
        require(!runtime.paused() && !runtime.placing(), "Escape did not cancel tower placement");
        escape(engine, runtime, id);
        require(runtime.paused(), "Escape did not pause after placement was canceled");
        escape(engine, runtime, id);
        require(!runtime.paused(), "Escape did not resume");
        const auto hero = runtime.heroes()->heroes().front().id;
        const auto instancesBeforeSelection = engine.performance().instances;
        const auto objectsBeforeSelection = engine.scene()->objects().size();
        require(runtime.selectHero(hero), "hero roster selection failed");
        tick(engine, runtime);
        require(engine.performance().instances == instancesBeforeSelection + 1 &&
                    engine.scene()->objects().size() == objectsBeforeSelection,
                "Kit selection marker was not a separate render instance");
        runtime.clearSelection();
        tick(engine, runtime);
        require(engine.performance().instances == instancesBeforeSelection,
                "Kit selection marker survived deselection");
        require(runtime.selectHero(hero), "Hero reselection failed");
        runtime.startWave();
        tick(engine, runtime);
        auto& enemies = *runtime.navigation().enemies();
        EnemyTypeId rat = 0, boss = 0;
        for (EnemyTypeId type = 0; type < enemies.typeCount(); ++type) {
            if (enemies.definition(type).name == "Black Rat") {
                rat = type;
            }
            if (enemies.definition(type).boss) {
                boss = type;
            }
        }
        require(runtime.navigation().spawnAt(engine, rat, {{-5, 0}, {-5, 0}, 0}, 1) == 1,
                "animated UI fixture spawn failed");
        const auto actor = enemies.enemies().back().id;
        const auto instance = *enemies.find(actor)->instance;
        tick(engine, runtime, 5);
        pause(engine, runtime, id);
        require(runtime.paused() && !engine.cameraNavigationEnabled(), "Pause did not suspend gameplay/camera");
        const auto position = enemies.find(actor)->position;
        const auto elapsed = runtime.completionSeconds();
        const auto animation = engine.animation(instance).time();
        tick(engine, runtime, 30);
        require(enemies.find(actor)->position == position && runtime.completionSeconds() == elapsed &&
                    engine.animation(instance).time() == animation,
                "pause advanced gameplay time, movement or animation");
        control(engine, runtime, id, "Pause Menu", "Settings");
        require(ImGui::FindWindowByName("Settings")->Active && runtime.paused(), "settings did not preserve pause");
        maximumScale(engine, runtime, id);
        escape(engine, runtime, id);
        require(runtime.paused(), "closing settings unexpectedly resumed battle");
        control(engine, runtime, id, "Pause Menu", "Resume");
        require(!runtime.paused() && engine.cameraNavigationEnabled() && runtime.completionSeconds() > elapsed,
                "resume did not restore gameplay");
        require(runtime.selectHero(hero), "Hero selection after pause failed");
        escape(engine, runtime, id);
        require(!runtime.paused() && !runtime.selectedHero() && !runtime.inspectedTower(),
                "Escape did not clear selection before pausing");
        escape(engine, runtime, id);
        require(runtime.paused(), "Escape did not pause");
        control(engine, runtime, id, "Pause Menu", "Restart Level");
        require(!runtime.paused() && runtime.level().waves()->phase() == LevelPhase::preparation,
                "restart from pause retained paused/battle state");
        require(runtime.selectHero(runtime.heroes()->heroes().front().id), "responsive hero selection failed");
        runtime.startWave();
        require(runtime.navigation().spawnAt(engine, boss, {{-5, 0}, {-5, 0}, 0}, 1) == 1,
                "responsive boss spawn failed");
        for (const glm::ivec2 size : {glm::ivec2{1920, 1080}, {1280, 720}, {960, 540}, {800, 600}, {640, 480}}) {
            require(SDL_SetWindowSize(window, size.x, size.y), SDL_GetError());
            tick(engine, runtime, 5);
            layout();
        }
        require(SDL_SetWindowSize(window, 1280, 720), SDL_GetError());
        tick(engine, runtime, 5);
        runtime.attachProgress(unavailable);
        tick(engine, runtime, 2);
        const auto* shop = ImGui::FindWindowByName("Build Towers");
        require(std::abs(shop->Pos.x + shop->Size.x * 0.5F - ImGui::GetMainViewport()->GetCenter().x) < 1,
                "Tower shop is not centered");
        require(!ImGui::FindWindowByName("Hero Info") && !ImGui::FindWindowByName("Game Status"),
                "Removed hero/status information remained in the HUD");
        for (const auto* name : {"Round", "Objective Health", "Wave Controls"}) {
            require((ImGui::FindWindowByName(name)->Flags & ImGuiWindowFlags_NoBackground) != 0,
                    "Top HUD regained a background panel");
        }
        if (argc > 1) {
            SDL_RaiseWindow(window);
            for (int frame = 0; frame < 900; ++frame) {
                require(engine.update(), "UI preview closed");
                SDL_Delay(16);
            }
        }
        pause(engine, runtime, id);
        control(engine, runtime, id, "Pause Menu", "Main Menu");
        require(!runtime.paused() && !engine.scene(), "returning to main menu retained pause/world state");
        if (argc <= 1) {
            require(SDL_SetWindowSize(window, 640, 480), SDL_GetError());
            tick(engine, runtime, 5);
            control(engine, runtime, id, "Main Menu", "Choose Level");
            auto* selection = ImGui::FindWindowByName("Level Selection");
            require(selection && selection->Active, "small-window level selection did not open");
            const auto* v = ImGui::GetMainViewport();
            require(selection->Pos.y >= v->Pos.y && selection->Pos.y + selection->Size.y <= v->Pos.y + v->Size.y + 1,
                    "small-window level selection exceeded viewport");
            const ImGuiWindow* cards = nullptr;
            for (const auto* child : ImGui::GetCurrentContext()->Windows) {
                if (child->Active && child->ParentWindow == selection) {
                    cards = child;
                    break;
                }
            }
            require(cards && cards->ScrollMax.y < 1 && cards->ScrollMax.x < 1,
                    "Single level card did not fit the small-window content area");
            control(engine, runtime, id, "Level Selection", "Main Menu");
        } else {
            for (int frame = 0; frame < 600; ++frame) {
                require(engine.update(), "menu preview closed");
                SDL_Delay(16);
            }
        }
        std::cout << "PASS polished HUD, pause/settings/resume/restart, frozen animation/timing, and 640p-to-1080p "
                     "layouts.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Game UI: " << error.what() << '\n';
        return 1;
    }
}
