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

void mouse(SDL_WindowID window, Uint32 type, glm::vec2 point, Uint8 button = SDL_BUTTON_LEFT) {
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.windowID = window;
        event.motion.x = point.x;
        event.motion.y = point.y;
    } else {
        event.button.windowID = window;
        event.button.button = button;
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
    // Clear the preceding hover before searching, including consecutive clicks on the same control.
    mouse(window, SDL_EVENT_MOUSE_MOTION, {1, 1});
    tick(engine, runtime);
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

glm::vec2 heroPoint(Engine& engine, SDL_Window* window, const Hero& hero) {
    glm::ivec2 size{};
    SDL_GetWindowSize(window, &size.x, &size.y);
    for (float height = 0.15F; height < 1.25F; height += 0.08F) {
        for (float offset = -0.24F; offset < 0.25F; offset += 0.06F) {
            const auto point = screen(engine, window, glm::vec3{hero.position} + glm::vec3{offset, height, 0});
            const auto ray = mouseRay(engine.scene()->resolvedCamera(), point, size, size);
            if (ray && engine.pickObject(*ray) == hero.object) {
                return point;
            }
        }
    }
    throw std::runtime_error("hero model contains no selectable visible geometry");
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
        definition.startingGold = 200;
        definition.waves.resize(1);
        definition.waves[0].goldReward = 7;
        definition.waves[0].groups[0].count = 2;
        definition.waves[0].groups[0].batchSize = 1;
        definition.waves[0].groups[0].interval = 0.1;
        definition.waves[0].groups[0].start = 20;
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
        require(runtime.level().economy().gold() == 200 &&
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
        control(engine, runtime, id, "Build Towers", "Small Tower", "basic");
        require(runtime.placing() && runtime.navigation().towers().towers().empty() &&
                    runtime.level().economy().gold() == 200,
                "HUD click placed a tower or charged before confirmation");
        const auto point = screen(engine, window, {-3.3F, 0, 0});
        mouse(id, SDL_EVENT_MOUSE_MOTION, point);
        tick(engine, runtime);
        require(runtime.preview() && runtime.placementError().empty(), "game placement preview is not valid");
        click(engine, runtime, id, point);
        require(runtime.navigation().towers().towers().size() == 1 && runtime.level().economy().gold() == 170 &&
                    !runtime.navigation().navigation().contains({-3.3, 0, 0}),
                "game construction or navigation update failed");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        tick(engine, runtime);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        tick(engine, runtime);
        require(!runtime.placing(), "Escape did not cancel gameplay placement");
        click(engine, runtime, id, screen(engine, window, {-3.3F, 0.75F, 0}));
        require(runtime.inspectedTower(), "gameplay tower selection failed");
        control(engine, runtime, id, "Build Towers", "Large Tower", "wide");
        const auto largePoint = screen(engine, window, {0.1F, 0, 0});
        mouse(id, SDL_EVENT_MOUSE_MOTION, largePoint);
        tick(engine, runtime);
        require(runtime.preview() && runtime.placementError().empty(), "Large Tower preview invalid");
        click(engine, runtime, id, largePoint);
        require(runtime.navigation().towers().towers().size() == 2 && runtime.level().economy().gold() == 120,
                "Large Tower construction failed");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        tick(engine, runtime);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        tick(engine, runtime);
        click(engine, runtime, id, screen(engine, window, {-3.3F, 0.75F, 0}));
        require(runtime.inspectedTower(), "upgrade tower selection failed");
        const auto smallTower = runtime.inspectedTower()->object;
        const auto revision = runtime.navigation().revision();
        control(engine, runtime, id, "Tower Info", "Upgrade");
        require(runtime.navigation().towers().progress(smallTower).level == 2 &&
                    runtime.level().economy().gold() == 95 && runtime.inspectedTower()->object == smallTower,
                "first HUD upgrade, payment or stable selection failed");
        control(engine, runtime, id, "Tower Info", "Upgrade");
        require(runtime.navigation().towers().progress(smallTower).level == 3 &&
                    runtime.level().economy().gold() == 55 && runtime.navigation().revision() == revision &&
                    runtime.level().upgradeError(runtime.navigation(), smallTower) == "MAX LEVEL",
                "maximum HUD upgrade rebuilt navigation or used incorrect statistics/payment");
        const auto rejectUpgrade = [&](ObjectId tower) {
            const auto gold = runtime.level().economy().gold();
            const auto level = runtime.navigation().towers().progress(tower).level;
            bool rejected = false;
            try {
                runtime.level().upgradeTower(engine, runtime.navigation(), tower);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected && runtime.level().economy().gold() == gold &&
                        runtime.navigation().towers().progress(tower).level == level,
                    "failed upgrade changed gold or tower level");
        };
        rejectUpgrade(smallTower);
        click(engine, runtime, id, screen(engine, window, {0.1F, 0.6F, 0}));
        require(runtime.inspectedTower() && runtime.inspectedTower()->type == "wide", "Large Tower selection failed");
        const auto largeTower = runtime.inspectedTower()->object;
        require(runtime.navigation().towers().progress(largeTower).level == 1, "one tower upgraded another");
        control(engine, runtime, id, "Tower Info", "Upgrade");
        require(runtime.navigation().towers().progress(largeTower).level == 2 && runtime.level().economy().gold() == 15,
                "Large Tower upgrade incorrect");
        rejectUpgrade(largeTower);
        require(runtime.heroes() && runtime.heroes()->heroes().size() == 1, "configured hero spawn missing");
        const auto heroId = runtime.heroes()->heroes().front().id;
        const auto heroObject = runtime.heroes()->heroes().front().object;
        const auto goldBeforeDeploy = runtime.level().economy().gold();
        control(engine, runtime, id, "Build Towers", "Deploy Hero");
        require(runtime.deployingHero(), "Deploy Hero did not enter placement");
        const auto deployment = screen(engine, window, {4, 0, -3});
        mouse(id, SDL_EVENT_MOUSE_MOTION, deployment);
        tick(engine, runtime);
        require(runtime.heroError().empty(), "valid hero placement rejected");
        click(engine, runtime, id, deployment);
        require(!runtime.deployingHero() && runtime.selectedHero() && runtime.selectedHero()->id == heroId &&
                    runtime.selectedHero()->object == heroObject &&
                    runtime.level().economy().gold() == goldBeforeDeploy &&
                    glm::length(glm::dvec2{runtime.selectedHero()->position.x - 4,
                                           runtime.selectedHero()->position.z + 3}) < 0.05,
                "free hero repositioning, identity or selection failed");
        const auto initialHeroPosition = runtime.selectedHero()->position;
        runtime.update(engine, 1);
        require(runtime.selectedHero()->position == initialHeroPosition, "preparation hero moved");
        control(engine, runtime, id, "Game Status", "Start Wave");
        require(runtime.level().waves()->phase() == LevelPhase::battle, "HUD did not start the wave");
        require(runtime.level().upgradeError(runtime.navigation(), largeTower) ==
                    "Upgrades are only available during preparation.",
                "Battle did not disable upgrades");
        rejectUpgrade(largeTower);
        click(engine, runtime, id, heroPoint(engine, window, *runtime.heroes()->find(heroId)));
        require(runtime.selectedHero() && engine.selectedObject() == heroObject, "hero picking/highlighting failed");
        const auto destination = screen(engine, window, {-5, 0, -1});
        const auto cameraBeforeClick = engine.scene()->resolvedCamera();
        mouse(id, SDL_EVENT_MOUSE_MOTION, destination);
        tick(engine, runtime);
        mouse(id, SDL_EVENT_MOUSE_BUTTON_DOWN, destination, SDL_BUTTON_RIGHT);
        tick(engine, runtime);
        mouse(id, SDL_EVENT_MOUSE_BUTTON_UP, destination, SDL_BUTTON_RIGHT);
        tick(engine, runtime);
        require(runtime.selectedHero()->destination &&
                    engine.scene()->resolvedCamera().position == cameraBeforeClick.position,
                "short right click failed to command hero or panned camera");
        for (int frame = 0; frame < 1000 && runtime.selectedHero()->state == HeroState::moving; ++frame) {
            runtime.update(engine, 1.0 / 60);
            require(runtime.heroes()->navigation().contains(runtime.selectedHero()->position),
                    "hero crossed an obstacle");
        }
        require(runtime.selectedHero()->state == HeroState::reached &&
                    glm::length(glm::dvec2{runtime.selectedHero()->position.x + 5,
                                           runtime.selectedHero()->position.z + 1}) < 0.15,
                "game hero failed to navigate around towers/walls");
        const glm::vec2 gesturePoint{700, 400};
        const auto drag = [&](Uint8 button, glm::vec2 motion) {
            mouse(id, SDL_EVENT_MOUSE_MOTION, gesturePoint);
            tick(engine, runtime);
            mouse(id, SDL_EVENT_MOUSE_BUTTON_DOWN, gesturePoint, button);
            tick(engine, runtime);
            SDL_Event moved{};
            moved.type = SDL_EVENT_MOUSE_MOTION;
            moved.motion.windowID = id;
            moved.motion.x = gesturePoint.x;
            moved.motion.y = gesturePoint.y;
            moved.motion.xrel = motion.x;
            moved.motion.yrel = motion.y;
            require(SDL_PushEvent(&moved), SDL_GetError());
            tick(engine, runtime);
            mouse(id, SDL_EVENT_MOUSE_BUTTON_UP, gesturePoint, button);
            tick(engine, runtime);
        };
        const auto beforePan = engine.scene()->resolvedCamera();
        drag(SDL_BUTTON_RIGHT, {40, 10});
        const auto afterPan = engine.scene()->resolvedCamera();
        require(afterPan.position != beforePan.position && !runtime.selectedHero()->destination &&
                    runtime.selectedHero()->state == HeroState::reached,
                "right drag did not pan or issued a hero command");
        drag(SDL_BUTTON_MIDDLE, {30, 12});
        const auto afterOrbit = engine.scene()->resolvedCamera();
        require(afterOrbit.position != afterPan.position && afterOrbit.target == afterPan.target,
                "middle drag did not preserve camera orbit");
        mouse(id, SDL_EVENT_MOUSE_MOTION, gesturePoint);
        tick(engine, runtime);
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.windowID = id;
        wheel.wheel.y = 1;
        wheel.wheel.mouse_x = gesturePoint.x;
        wheel.wheel.mouse_y = gesturePoint.y;
        require(SDL_PushEvent(&wheel), SDL_GetError());
        tick(engine, runtime);
        const auto zoomed = engine.scene()->resolvedCamera();
        require(glm::length(zoomed.position - zoomed.target) < glm::length(afterOrbit.position - afterOrbit.target) &&
                    !runtime.selectedHero()->destination,
                "zoom changed hero destination or failed");
        mouse(id, SDL_EVENT_MOUSE_MOTION, {50, 40});
        tick(engine, runtime);
        mouse(id, SDL_EVENT_MOUSE_BUTTON_DOWN, {50, 40}, SDL_BUTTON_RIGHT);
        tick(engine, runtime);
        mouse(id, SDL_EVENT_MOUSE_BUTTON_UP, {50, 40}, SDL_BUTTON_RIGHT);
        tick(engine, runtime);
        require(!runtime.selectedHero()->destination && runtime.selectedHero()->state == HeroState::reached,
                "gameplay UI right click commanded hero");
        auto& enemies = *runtime.navigation().enemies();
        const auto& mesh = runtime.navigation().surface(engine, 0);
        require(enemies.spawn(mesh, {{-5, -2}, {-5, -2}, 0}, 1) == 1, "Game hero target spawn failed");
        const auto enemyId = enemies.enemies().back().id;
        glm::ivec2 windowSize{};
        SDL_GetWindowSize(window, &windowSize.x, &windowSize.y);
        std::optional<glm::vec2> enemyPixel;
        for (float height = 0.05F; height < 0.4F && !enemyPixel; height += 0.03F) {
            for (float offset = -0.2F; offset <= 0.2F; offset += 0.04F) {
                const auto point = screen(engine, window, {-5 + offset, height, -2});
                const auto ray = mouseRay(engine.scene()->resolvedCamera(), point, windowSize, windowSize);
                if (ray && HeroCombatSystem::pick(engine, enemies, *ray) == enemyId) {
                    enemyPixel = point;
                    break;
                }
            }
        }
        require(enemyPixel.has_value(), "Game enemy geometry was not pickable");
        mouse(id, SDL_EVENT_MOUSE_MOTION, *enemyPixel);
        tick(engine, runtime);
        mouse(id, SDL_EVENT_MOUSE_BUTTON_DOWN, *enemyPixel, SDL_BUTTON_RIGHT);
        tick(engine, runtime);
        mouse(id, SDL_EVENT_MOUSE_BUTTON_UP, *enemyPixel, SDL_BUTTON_RIGHT);
        tick(engine, runtime);
        require(runtime.selectedHero()->combat.target == enemyId && runtime.selectedHero()->combat.explicitTarget,
                "Game right-click enemy did not issue explicit attack");
        require(runtime.heroes()->command(runtime.selectedHero()->id, {-5, 0, -1}), "Game combat move override failed");
        tick(engine, runtime);
        control(engine, runtime, id, "Hero Info", "Cleave");
        require(runtime.selectedHero()->combat.cleaveCooldown > 0 && runtime.selectedHero()->combat.abilitySwing,
                "Game Cleave HUD did not activate ability");
        require(enemies.remove(enemyId), "Game hero target cleanup failed");
        control(engine, runtime, id, "Hero Info", "Cancel Order");
        require(!runtime.selectedHero()->combat.swinging && !runtime.selectedHero()->combat.playerMoving &&
                    !runtime.selectedHero()->combat.target && runtime.selectedHero()->combat.cleaveCooldown > 0,
                "Game cancel order failed or refunded ability cooldown");
        require(runtime.heroes()->applyDamage(runtime.selectedHero()->id, 1), "Game hero hit feedback setup failed");
        for (const auto& tower : runtime.navigation().towers().towers()) {
            require(runtime.navigation().towers().applyDamage(engine, tower.object, 1),
                    "Game tower health setup failed");
        }
        tick(engine, runtime);
        require(runtime.selectedHero()->health == 99, "Game hero health HUD state incorrect");
        finishBattle(engine, runtime);
        const auto* resultPanel = ImGui::FindWindowByName("Level Result");
        const auto& metrics = runtime.navigation().enemies()->metrics();
        require(metrics.defeated > 0 && metrics.arrived + metrics.defeated == 2 &&
                    runtime.level().combat()->shotsFired() > 0,
                "standalone Game towers did not defeat enemies");
        require(runtime.level().waves()->phase() == LevelPhase::victory &&
                    runtime.level().waves()->health() == 30 - metrics.arrived &&
                    runtime.level().economy().gold() == 22 && resultPanel && resultPanel->Active,
                "Game victory, reward or result HUD failed");
        require(contents(levelPath) == levelBefore && contents(assetRoot / definition.scene) == sceneBefore,
                "gameplay overwrote authored files");
        control(engine, runtime, id, "Level Result", "Play Again");
        require(
            runtime.level().economy().gold() == 200 && runtime.level().waves()->health() == 30 &&
                runtime.navigation().towers().towers().empty() && runtime.navigation().enemies()->enemies().empty() &&
                runtime.navigation().enemies()->deathEffectCount() == 0 &&
                runtime.level().combat()->shotsFired() == 0 && runtime.navigation().navigation().contains({-3.3, 0, 0}),
            "game restart retained runtime state");
        require(runtime.heroes()->heroes().size() == 1 && !runtime.selectedHero() &&
                    runtime.heroes()->heroes().front().health == 100 &&
                    runtime.heroes()->heroes().front().state == HeroState::idle &&
                    glm::length(glm::dvec2{runtime.heroes()->heroes().front().position.x - 4,
                                           runtime.heroes()->heroes().front().position.z - 2}) < 0.05,
                "restart did not restore configured hero position/state");
        auto rebuilt = runtime.navigation().towers().definition("basic").instance();
        rebuilt.position = {-3.3, rebuilt.halfExtent.y, 0};
        const auto rebuiltId = runtime.level().buildTower(engine, runtime.navigation(), rebuilt);
        require(runtime.navigation().towers().progress(rebuiltId).level == 1, "restart retained tower upgrades");
        definition.objectiveHealth = 1;
        definition.heroSpawns.clear();
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
