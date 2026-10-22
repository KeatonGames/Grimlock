#include "debug/navigation_debug.hpp"
#include "editor.hpp"
#include "navigation/nav_mesh.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <grimlock/terrain.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>

using namespace grimlock;

namespace {

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void tick(Engine& engine, int count = 2) {
    for (int i = 0; i < count; ++i) {
        require(engine.update(), "navigation editor closed");
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

void click(Engine& engine, SDL_WindowID window, glm::vec2 point) {
    mouse(window, SDL_EVENT_MOUSE_MOTION, point);
    tick(engine);
    mouse(window, SDL_EVENT_MOUSE_MOTION, point);
    mouse(window, SDL_EVENT_MOUSE_BUTTON_DOWN, point);
    tick(engine);
    mouse(window, SDL_EVENT_MOUSE_MOTION, point);
    mouse(window, SDL_EVENT_MOUSE_BUTTON_UP, point);
    tick(engine);
}

void popupControl(Engine& engine, SDL_WindowID window, const char* label) {
    auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
    require(!popups.empty() && popups.back().Window, "Navigation menu missing");
    const auto* popup = popups.back().Window;
    const auto control = ImHashStr(label, 0, popup->ID);
    bool found = false;
    for (float y = popup->DC.CursorStartPos.y + 4; y < popup->ClipRect.Max.y; y += 4) {
        for (const float x : {60.0F, 230.0F}) {
            const glm::vec2 point{popup->DC.CursorStartPos.x + x, y};
            mouse(window, SDL_EVENT_MOUSE_MOTION, point);
            tick(engine, 1);
            if (ImGui::GetHoveredID() == control) {
                click(engine, window, point);
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }
    require(found, label);
}

void navigationControl(Engine& engine, SDL_WindowID window, const char* label, const char* choice = nullptr) {
    std::cout << "Control: " << label << std::endl;
    click(engine, window, {130, 11});
    popupControl(engine, window, label);
    if (choice) {
        popupControl(engine, window, choice);
    }
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    tick(engine);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    tick(engine);
    // Escape leaves keyboard focus on the menu bar; return to the viewport before reopening it.
    click(engine, window, {700, 680});
}

void checkAnimatedEnemies(Engine& engine, const Grid& grid) {
    game::EnemyDefinition definition;
    definition.model = "models/blackrat.glb";
    definition.movementAnimation = "idle_A1";
    const auto& c = grid.configuration();
    game::NavMesh navigation;
    require(navigation.build(game::navigationGeometry(terrainBoxes(grid))), navigation.status().c_str());
    definition.radius = 0.2;
    const glm::dvec2 destination{c.origin.x + c.width - 0.37, c.origin.z + c.height - 0.29};
    std::shared_ptr<const assets::ModelData> resource;
    long baselineOwners = 0;
    {
        game::EnemySystem system{engine, definition};
        require(system.spawn(navigation,
                             {{c.origin.x + c.cellSize, c.origin.z + c.cellSize},
                              {c.origin.x + c.cellSize * 4, c.origin.z + c.cellSize * 4},
                              c.origin.y},
                             3) == 3,
                "animated enemy spawn failed");
        const auto enemies = system.enemies();
        const auto first = *enemies[0].instance, second = *enemies[1].instance;
        auto& a = engine.animation(first);
        auto& b = engine.animation(second);
        resource = a.asset();
        baselineOwners = resource.use_count() - 3;
        require(resource == b.asset() && &a.pose() != &b.pose() && a.paused() && b.paused() && a.time() != b.time(),
                "enemy animation resources or playback state are not independent");
        const auto time = a.time();
        for (int frame = 0; frame < 20; ++frame) {
            require(engine.update([&](double) {
                system.update(navigation, destination, 1.0 / 60);
            }),
                    "animated enemy frame failed");
        }
        require(a.playing() && b.playing() && a.time() != time, "movement animation did not advance");
        system.setSpeed(0);
        system.update(navigation, destination, 0.25);
        require(a.paused() && b.paused(), "stationary enemies kept playing their movement animation");
        const auto stopped = a.time();
        tick(engine, 3);
        require(a.time() == stopped, "paused enemy animation advanced");
        const auto firstId = enemies[0].id, removedId = enemies[1].id, lastId = enemies[2].id;
        require(system.remove(removedId) && !system.remove(removedId), "enemy removal did not invalidate its ID");
        require(system.enemies().size() == 2 && system.enemies()[0].id == firstId && system.enemies()[1].id == lastId,
                "packed removal changed surviving enemy IDs");
        system.setSpeed(1);
        system.update(navigation, destination, 1.0 / 60);
        require(a.playing(), "moving enemy animation did not resume");
        require(system.setDestination(firstId, std::nullopt), "individual enemy lookup failed");
        const auto* last = system.find(lastId);
        require(last && !system.find(removedId), "stable enemy lookup survived removal incorrectly");
        const glm::dvec2 individualGoal{last->position.x, last->position.z};
        require(system.setDestination(lastId, individualGoal), "individual destination assignment failed");
        system.update(navigation, destination, 1.0 / 60);
        require(system.find(firstId)->state == game::EnemyState::idle && a.paused() &&
                    system.find(lastId)->state == game::EnemyState::reached &&
                    engine.animation(*system.find(lastId)->instance).paused(),
                "independent Idle/Reached states or animation playback incorrect");
        require(system.find(lastId)->destination == individualGoal,
                "per-frame shared destination overwrote an individual assignment");
        system.setSharedDestination(destination);
        system.update(navigation, destination, 1.0 / 60);
        require(system.find(firstId)->state == game::EnemyState::moving && a.playing(),
                "shared retarget did not resume idle enemy animation");
    }
    require(resource.use_count() == baselineOwners, "enemy system destruction retained animation instances");
}

void checkAnimalSelection(Engine& engine, game::NavigationDebug& debug, SDL_WindowID window) {
    auto& session = debug.session();
    auto& system = *session.enemies();
    session.reset(engine);
    require(system.typeCount() == 11 && system.definition().name == "Chicken", "animal catalog/default missing");
    require(session.spawn(engine, 2) == 2, "compact chicken spawn failed");
    const auto chicken = system.enemies()[0];
    const auto chickenRevision = chicken.navigation->revision();
    const auto transform = engine.transform(*chicken.instance);
    const double spacing = glm::length(chicken.position - system.enemies()[1].position);
    require(spacing >= chicken.radius * 2 && spacing < chicken.radius * 2 + 0.005,
            "chicken spawning retained excessive padding");

    navigationControl(engine, window, "Animal Model", "Dog");
    const auto dogType = session.selectedAnimal();
    require(system.definition(dogType).model == "models/dog.glb" && system.enemies().size() == 2 &&
                system.enemies()[0].radius == chicken.radius && system.enemies()[0].navigation == chicken.navigation &&
                chicken.navigation->revision() == chickenRevision && engine.transform(*chicken.instance) == transform,
            "selecting an animal modified an existing chicken");
    const double oldSpeed = system.enemies()[0].speed;
    system.setSpawnSpeed(0.8, 0);
    require(system.enemies()[0].speed == oldSpeed, "spawn speed edited an existing enemy");
    navigationControl(engine, window, "Spawn Animals");
    require(system.enemies().size() > 2 && system.enemies()[2].type == dogType &&
                system.enemies()[2].radius == system.definition(dogType).radius &&
                engine.animation(*system.enemies()[2].instance).asset() != engine.animation(*chicken.instance).asset(),
            "model dropdown did not affect spawned model and physical settings");
    for (std::size_t i = 0; i < system.enemies().size(); ++i) {
        for (std::size_t j = i + 1; j < system.enemies().size(); ++j) {
            require(glm::length(system.enemies()[i].position - system.enemies()[j].position) + 1e-5 >=
                        system.enemies()[i].radius + system.enemies()[j].radius,
                    "mixed spawn footprints overlapped");
        }
    }
    session.reset(engine);
    navigationControl(engine, window, "Animal Model", "Black Rat");
    require(session.spawn(engine, 2) == 2, "selected animated animal did not spawn");
    const auto first = *system.enemies()[0].instance;
    auto& playback = engine.animation(first);
    require(!playback.restPose() && playback.model().animations[playback.clip()].name.find("walk") != std::string::npos,
            "automatic movement animation did not prefer a walking loop");
    for (int frame = 0; frame < 30; ++frame) {
        require(engine.update([&](double) {
            session.update(engine, 1.0 / 60);
        }),
                "selected animal render failed");
    }
    require(playback.playing(), "selected animal movement animation did not resume");
    session.reset(engine);

    std::cout << "Checking every configured animal asset..." << std::endl;
    session.setDestination(std::nullopt);
    for (game::EnemyTypeId type = 0; type < system.typeCount(); ++type) {
        session.selectAnimal(engine, type);
        require(session.spawn(engine, 2) == 2, "configured animal did not spawn");
        const auto& a = system.enemies()[system.enemies().size() - 2];
        const auto& b = system.enemies().back();
        require(a.type == type && b.type == type && a.navigation == b.navigation &&
                    engine.animation(*a.instance).asset() == engine.animation(*b.instance).asset(),
                "animal type/model resources were not shared");
        const auto info = engine.modelInfo(engine.loadModel(engine.assetRoot() / system.definition(type).model));
        const auto size =
            (info.maximum - info.minimum) * (system.definition(type).visualHeight / (info.maximum.y - info.minimum.y));
        std::cout << system.definition(type).name << " visual bounds " << size.x << ',' << size.y << ',' << size.z
                  << "; physical radius " << system.definition(type).radius << '\n';
        require(system.definition(type).radius >= glm::length(glm::dvec2{size.x, size.z}) * 0.5,
                "configured footprint does not cover the animal's framed XZ dimensions");
        require(engine.update([&](double) {
            session.update(engine, 1.0 / 60);
        }),
                "animal asset render failed");
    }
    const auto oldRevision = chicken.navigation->revision();
    session.rebuild(engine);
    require(chicken.navigation->revision() > oldRevision, "mixed navigation rebuild missed chicken profile");
    for (const auto& enemy : system.enemies()) {
        require(enemy.navigation->ready(), "mixed rebuild lost a navigation profile");
    }
    session.reset(engine);
    session.selectAnimal(engine, 0);
    system.setSpawnSpeed(1.5, 0);
}

void checkAnimalBatches(Engine& engine, game::NavigationDebug& debug, SDL_WindowID window) {
    auto& session = debug.session();
    auto& system = *session.enemies();
    session.reset(engine);
    session.setDestination(std::nullopt);
    navigationControl(engine, window, "Animal Model", "Black Rat");
    require(session.spawn(engine, 100) == 100, "default rat batch did not fit");
    require(engine.update([&](double) {
        session.update(engine, 1.0 / 60);
    }),
            "100-rat frame failed");
    require(session.spawn(engine, 200) == 200, "larger rat batch did not fit");
    const auto asset = engine.animation(*system.enemies()[0].instance).asset();
    for (const auto& enemy : system.enemies()) {
        require(engine.animation(*enemy.instance).asset() == asset, "rat batch duplicated model resources");
    }
    for (int frame = 0; frame < 3; ++frame) {
        require(engine.update([&](double) {
            session.update(engine, 1.0 / 60);
        }),
                "300-rat frame failed");
    }
    session.reset(engine);
    for (game::EnemyTypeId type = 0; type < system.typeCount(); ++type) {
        if (system.definition(type).model == "models/bunny2.glb") {
            session.selectAnimal(engine, type);
            break;
        }
    }
    require(session.spawn(engine, 200) == 200, "large multi-primitive animal batch did not fit");
    for (int frame = 0; frame < 3; ++frame) {
        require(engine.update([&](double) {
            session.update(engine, 1.0 / 60);
        }),
                "200-bunny frame failed");
    }
    session.reset(engine);
    session.selectAnimal(engine, 0);
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected Game source and output scene paths");
        const std::filesystem::path game{argv[1]}, output{argv[2]};
        Engine engine{EngineConfig{.windowTitle = "Enemy runtime checks", .assetRoot = game / "assets"}};
        game::NavigationDebug debug;
        editor::Editor editor{engine, game / "assets/scenes/chickens.json", &debug};
        tick(engine, 4);
        auto* scene = engine.scene();
        auto* grid = scene->navigationGrid();
        const auto objectCount = scene->objects().size();
        require(grid && objectCount == 4, "corridor scenario missing");
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        require(windows && count == 1, "navigation window missing");
        auto* window = windows[0];
        const auto id = SDL_GetWindowID(window);
        SDL_free(windows);
        SDL_Event focus{};
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        focus.window.windowID = id;
        require(SDL_PushEvent(&focus), SDL_GetError());
        tick(engine);

        require(ImGui::GetBackgroundDrawList(ImGui::GetMainViewport())->VtxBuffer.empty(),
                "grid visible during normal viewing");
        navigationControl(engine, id, "Edit Walkability");
        require(!ImGui::GetBackgroundDrawList(ImGui::GetMainViewport())->VtxBuffer.empty(),
                "editing did not show the grid");
        navigationControl(engine, id, "Edit Walkability");
        require(ImGui::GetBackgroundDrawList(ImGui::GetMainViewport())->VtxBuffer.empty(),
                "leaving edit mode kept the grid visible");
        require(debug.session().navigation().ready(), "scene did not build navigation geometry");
        const auto revision = debug.session().navigation().revision();
        navigationControl(engine, id, "Show NavMesh");
        require(!ImGui::GetBackgroundDrawList(ImGui::GetMainViewport())->VtxBuffer.empty(),
                "NavMesh overlay not drawn");
        navigationControl(engine, id, "Rebuild NavMesh");
        require(debug.session().navigation().revision() > revision, "explicit rebuild did not replace navigation data");
        navigationControl(engine, id, "Show NavMesh");
        require(ImGui::GetBackgroundDrawList(ImGui::GetMainViewport())->VtxBuffer.empty(),
                "NavMesh overlay stayed visible");
        std::cout << "Checking 100 chicken spawn..." << std::endl;
        navigationControl(engine, id, "Spawn Animals");
        require(debug.session().enemies()->enemies().size() == 100 && scene->objects().size() == objectCount &&
                    !editor.document().dirty(),
                "spawn failed or added serialized/dirty scene objects");
        navigationControl(engine, id, "Inspect Enemy", "Clear Enemy Destination");
        debug.update(engine, 0);
        require(debug.session().enemies()->enemies()[0].state == game::EnemyState::idle,
                "enemy inspector did not clear the selected destination");
        debug.session().setDestination(debug.session().destination());
        const auto animationTime = scene->animation(4).time();
        const auto firstId = debug.session().enemies()->enemies()[0].id;
        const auto firstHandle = *debug.session().enemies()->enemies()[0].instance;
        const auto sharedAsset = engine.animation(firstHandle).asset();
        const auto baselineOwners = sharedAsset.use_count() - 100;
        const auto initialPosition = debug.session().enemies()->enemies()[0].position;
        for (const auto& enemy : debug.session().enemies()->enemies()) {
            require(enemy.active && enemy.alive && enemy.health == 100 &&
                        engine.animation(*enemy.instance).asset() == sharedAsset &&
                        engine.animation(*enemy.instance).restPose(),
                    "enemy data, shared chicken resource or missing-animation fallback incorrect");
        }
        std::vector<double> frameTimes;
        frameTimes.reserve(120);
        double movementMilliseconds = 0;
        bool sceneAnimationAdvanced = false;
        std::cout << "Checking shared agent movement..." << std::endl;
        for (int frame = 0; frame < 120; ++frame) {
            const auto start = std::chrono::steady_clock::now();
            require(engine.update([&](double) {
                debug.update(engine, 1.0 / 60);
            }),
                    "movement frame failed");
            frameTimes.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            movementMilliseconds += debug.session().enemies()->metrics().updateMilliseconds;
            sceneAnimationAdvanced = sceneAnimationAdvanced || scene->animation(4).time() != animationTime;
            for (const auto& agent : debug.session().enemies()->enemies()) {
                require(debug.session().navigation().contains(agent.position),
                        "runtime agent entered a blocked cell or wall");
            }
        }
        require(debug.session().enemies()->enemies()[0].position != initialPosition, "chicken crowd did not move");
        std::sort(frameTimes.begin(), frameTimes.end());
        std::cout << "100 chickens: CPU update average " << movementMilliseconds / 120 << " ms; frame median "
                  << frameTimes[60] << " ms; p95 " << frameTimes[114] << " ms.\n";
        // A looping clip may wrap below its starting time during the measured frames.
        require(sceneAnimationAdvanced && scene->animation(4).playing(), "existing scene animation stopped");

        std::cout << "Checking live wall and destination edits..." << std::endl;
        const auto occupied = *grid->worldToCell(debug.session().enemies()->enemies()[0].position);
        editor.document().setGridWalkable(occupied, false);
        const auto oldRevision = debug.session().navigation().revision();
        const auto stoppedPosition = debug.session().enemies()->enemies()[0].position;
        require(engine.update([&](double) {
            debug.update(engine, 0.1);
        }),
                "edited wall frame failed");
        require(debug.session().enemies()->enemies()[0].movement == game::MovementStatus::blocked &&
                    debug.session().enemies()->enemies()[0].state == game::EnemyState::blocked &&
                    debug.session().enemies()->enemies()[0].position == stoppedPosition &&
                    debug.session().navigation().revision() != oldRevision,
                "painted wall did not stop the overlapping enemy");
        editor.document().setGridWalkable(occupied, true);
        const glm::dvec2 worldDestination{grid->configuration().origin.x + 2.37,
                                          grid->configuration().origin.z + 14.19};
        debug.session().setDestination(worldDestination);
        require(engine.update([&](double) {
            debug.update(engine, 0.1);
        }),
                "destination update failed");
        require(debug.session().destination() == worldDestination, "world destination was snapped to a cell");
        engine.saveScene(output);
        require(Scene::load(output)->objects().size() == objectCount, "test agents leaked into scene serialization");
        std::cout << "Checking test agent reset..." << std::endl;
        navigationControl(engine, id, "Clear Animals");
        require(debug.session().enemies()->enemies().empty(), "reset left active runtime instances");
        require(sharedAsset.use_count() == baselineOwners, "clearing enemies retained model instance ownership");
        bool invalidated = false;
        try {
            (void)engine.animation(firstHandle);
        } catch (const std::out_of_range&) {
            invalidated = true;
        }
        require(invalidated, "removed instance handle stayed valid");
        require(engine.update([&](double) {
            debug.update(engine, 0);
        }),
                "reset frame failed");

        std::cout << "Checking scene replacement..." << std::endl;
        for (int cycle = 0; cycle < 5; ++cycle) {
            require(debug.session().spawn(engine) == 100, "repeat spawn lost capacity");
            require(debug.session().enemies()->enemies()[0].id > firstId &&
                        engine.animation(*debug.session().enemies()->enemies()[0].instance).asset() == sharedAsset,
                    "enemy ID reused or chicken model duplicated");
            bool stale = false;
            try {
                (void)engine.animation(firstHandle);
            } catch (const std::out_of_range&) {
                stale = true;
            }
            require(stale, "repeat spawn resurrected a stale instance handle");
            debug.session().reset(engine);
            require(sharedAsset.use_count() == baselineOwners, "repeated removal leaked instances");
        }
        std::cout << "Checking physical radius independent of model scale..." << std::endl;
        auto& system = *debug.session().enemies();
        require(debug.session().spawn(engine, 2) == 2, "radius comparison spawn failed");
        const auto smallTransform = engine.transform(*system.enemies()[0].instance);
        const double smallSpacing = glm::length(system.enemies()[0].position - system.enemies()[1].position);
        require(system.enemies()[0].radius == system.definition().radius &&
                    system.enemies()[1].radius == system.definition().radius,
                "spawn ignored the shared physical radius");
        debug.session().reset(engine);
        auto largerDefinition = system.definition();
        largerDefinition.radius = 0.35;
        {
            game::EnemySystem larger{engine, largerDefinition};
            game::NavMesh navigation;
            auto config = navigation.configuration();
            config.agentRadius = 0.35F;
            require(navigation.build(game::navigationGeometry(terrainBoxes(*grid)), config),
                    navigation.status().c_str());
            require(larger.spawn(navigation, debug.session().spawnArea(), 2) == 2, "larger definition spawn failed");
            const auto largeTransform = engine.transform(*larger.enemies()[0].instance);
            const double largeSpacing = glm::length(larger.enemies()[0].position - larger.enemies()[1].position);
            require(larger.enemies()[0].radius == 0.35 && glm::mat3{smallTransform} == glm::mat3{largeTransform} &&
                        largeSpacing > smallSpacing + 0.1,
                    "type radius changed visual scale or did not affect spacing");
        }
        require(sharedAsset.use_count() == baselineOwners, "alternate definition retained model instances");
        std::cout << "Checking 500 rendered chickens..." << std::endl;
        const auto& configuration = grid->configuration();
        debug.session().setDestination(glm::dvec2{configuration.origin.x + configuration.width - 0.63,
                                                  configuration.origin.z + configuration.height - 0.71});
        require(debug.session().spawn(engine, 500) == 500, "large chicken crowd did not spawn");
        require(sharedAsset.use_count() == baselineOwners + 500, "large crowd duplicated or retained model resources");
        movementMilliseconds = 0;
        frameTimes.clear();
        for (int frame = 0; frame < 120; ++frame) {
            const auto start = std::chrono::steady_clock::now();
            require(engine.update([&](double) {
                debug.update(engine, 1.0 / 60);
            }),
                    "large crowd frame failed");
            movementMilliseconds += system.metrics().updateMilliseconds;
            frameTimes.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            for (const auto& enemy : system.enemies()) {
                require(debug.session().navigation().contains(enemy.position), "rendered crowd entered a wall");
            }
        }
        std::sort(frameTimes.begin(), frameTimes.end());
        std::cout << "500 rendered chickens: CPU update average " << movementMilliseconds / 120 << " ms; frame median "
                  << frameTimes[60] << " ms; p95 " << frameTimes[114] << " ms.\n";
        debug.session().reset(engine);
        require(sharedAsset.use_count() == baselineOwners, "large crowd cleanup leaked instances");
        checkAnimalBatches(engine, debug, id);
        (void)debug.session().spawn(engine);
        editor.document().open(game / "assets/scenes/navigation.json");
        require(engine.update([&](double) {
            debug.update(engine, 0);
        }),
                "scene replacement frame failed");
        require(debug.session().enemies()->enemies().empty(), "scene replacement kept stale runtime agents");
        std::cout << "Checking animated configuration and enemy lifecycle..." << std::endl;
        checkAnimatedEnemies(engine, *engine.scene()->navigationGrid());
        checkAnimalSelection(engine, debug, id);
        std::cout << "Checking missing navigation geometry..." << std::endl;
        editor.document().open(game / "assets/scenes/demo.json");
        debug.update(engine, 0);
        require(!engine.scene()->navigationGrid() && !debug.session().navigation().ready() &&
                    debug.session().spawn(engine, 3) == 0,
                "missing geometry allowed unbounded navigation");
        debug.session().reset(engine);
        std::cout << "PASS enemy controls, edit-only overlay, world destinations, predefined radii, resources and "
                     "animations.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL enemy runtime: " << error.what() << '\n';
        return 1;
    }
}
