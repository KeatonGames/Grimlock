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

void navigationControl(Engine& engine,
                       SDL_WindowID window,
                       const char* label,
                       const char* choice = nullptr,
                       const char* detail = nullptr) {
    std::cout << "Control: " << label << std::endl;
    click(engine, window, {130, 11});
    popupControl(engine, window, label);
    if (choice) {
        popupControl(engine, window, choice);
    }
    if (detail) {
        popupControl(engine, window, detail);
    }
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    tick(engine);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    tick(engine);
    // Escape leaves keyboard focus on the menu bar; return to the viewport before reopening it.
    click(engine, window, {700, 680});
}

void panelControl(Engine& engine, SDL_WindowID window, const char* label) {
    auto* panel = ImGui::FindWindowByName("Level");
    require(panel && !panel->Collapsed, "level gameplay panel missing");
    const auto control = ImHashStr(label, 0, panel->ID);
    for (float y = panel->InnerClipRect.Min.y + 4; y < panel->InnerClipRect.Max.y; y += 4) {
        const glm::vec2 point{panel->DC.CursorStartPos.x + 60, y};
        mouse(window, SDL_EVENT_MOUSE_MOTION, point);
        tick(engine, 1);
        if (ImGui::GetHoveredID() == control) {
            click(engine, window, point);
            return;
        }
    }
    throw std::runtime_error(label);
}

void checkAnimatedEnemies(Engine& engine, const Grid& grid) {
    game::EnemyDefinition definition;
    definition.model = "models/blackrat.glb";
    definition.movementAnimation = "idle_A1";
    definition.despawnOnArrival = false;
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

    definition.despawnOnArrival = true;
    game::EnemySystem system{engine, definition};
    require(system.spawn(navigation,
                         {{c.origin.x + c.cellSize, c.origin.z + c.cellSize},
                          {c.origin.x + c.cellSize * 4, c.origin.z + c.cellSize * 4},
                          c.origin.y},
                         3) == 3,
            "arrival cleanup spawn failed");
    const auto handle = *system.enemies().front().instance;
    for (const auto& enemy : system.enemies()) {
        require(system.setDestination(enemy.id, glm::dvec2{enemy.position.x, enemy.position.z}),
                "arrival destination assignment failed");
    }
    system.update(navigation, std::nullopt, 1.0 / 60);
    require(system.enemies().empty() && system.metrics().arrived == 3 && resource.use_count() == baselineOwners,
            "arrival did not remove packed enemies and release animation instances");
    bool invalidated = false;
    try {
        (void)engine.animation(handle);
    } catch (const std::out_of_range&) {
        invalidated = true;
    }
    require(invalidated, "arrival retained a render instance");
    system.update(navigation, std::nullopt, 1.0 / 60);
    require(system.metrics().arrived == 3, "arrival was counted more than once");
}

void checkAnimalSelection(Engine& engine, game::NavigationDebug& debug, SDL_WindowID window) {
    auto& session = debug.session();
    auto& system = *session.enemies();
    session.reset(engine);
    require(system.typeCount() == 14 && system.definition().name == "Chicken", "animal catalog/default missing");
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

glm::vec2 screenPoint(Engine& engine, SDL_Window* window, glm::dvec3 point) {
    glm::ivec2 logical{}, drawable{};
    SDL_GetWindowSize(window, &logical.x, &logical.y);
    SDL_GetWindowSizeInPixels(window, &drawable.x, &drawable.y);
    const auto camera = engine.scene()->resolvedCamera();
    auto clip = glm::dmat4{camera.projection(static_cast<float>(drawable.x) / drawable.y)} * glm::dmat4{camera.view()} *
                glm::dvec4{point, 1};
    clip /= clip.w;
    return {static_cast<float>((clip.x + 1) * 0.5 * logical.x), static_cast<float>((clip.y + 1) * 0.5 * logical.y)};
}

void checkTowers(Engine& engine,
                 game::NavigationDebug& debug,
                 editor::Editor& editor,
                 SDL_WindowID windowId,
                 const std::filesystem::path& output) {
    std::cout << "Checking tower placement, route protection, persistence and removal..." << std::endl;
    auto scene = std::make_unique<Scene>("Tower corridor");
    scene->setNavigationGrid(GridConfiguration{12, 8, 1, {0, 0, 0}});
    game::BuildZone buildZone;
    buildZone.name = "Corridor construction area";
    buildZone.position = {6, 0, 4};
    buildZone.dimensions = {12, 8};
    (void)game::PlacementSystem::createZone(*scene, buildZone);
    for (std::uint32_t z = 0; z < 8; ++z) {
        if (z < 3 || z >= 5) {
            scene->navigationGrid()->setWalkable({5, z}, false);
        }
    }
    SceneCamera camera;
    camera.camera.position = {6, 30, 12};
    camera.camera.target = {6, 0, 4};
    scene->setCamera("main", camera);
    const auto prop = scene->createObject("Ordinary prop");
    scene->setModel(prop, "models/cube.obj");
    Transform propTransform;
    propTransform.position = {8, 0.5F, 6};
    propTransform.scale = {0.4F, 0.5F, 0.4F};
    scene->setTransform(prop, propTransform);
    engine.setScene(std::move(scene));
    auto& session = debug.session();
    session.synchronize(engine);
    const auto zoneCount = session.placement().zones().size();
    const auto beforeZoneEdit = session.navigation().revision();
    navigationControl(engine, windowId, "Build Zones", "Create Build Zone");
    require(session.placement().zones().size() == zoneCount + 1 && editor.document().dirty(),
            "build zone creation did not update the editor document");
    const auto newZone = session.placement().zones().back().object;
    engine.scene()->selectObject(newZone, true);
    navigationControl(engine, windowId, "Build Zones", "Delete Build Zone");
    require(session.placement().zones().size() == zoneCount && !engine.scene()->contains(newZone),
            "build zone deletion retained construction space");
    require(session.navigation().revision() == beforeZoneEdit, "zone-only editing rebuilt navigation");
    session.selectAnimal(engine, 0);
    session.spawnArea() = {{1, 3.3}, {2.5, 4.7}, 0};
    session.setDestination(glm::dvec2{10.25, 4});
    game::Tower propOverlap;
    propOverlap.position = {8, 0.75, 6};
    require(!session.checkTower(engine, propOverlap).empty(), "ordinary scene model overlap accepted");
    game::Tower cached;
    cached.position = {5.37, 0.75, 3.55};
    require(session.checkTower(engine, cached).empty(), "unused large animal profiles prevented chicken placement");
    game::EnemyTypeId dog = 0;
    while (dog < session.enemies()->typeCount() && session.enemies()->definition(dog).name != "Dog") {
        ++dog;
    }
    require(dog < session.enemies()->typeCount(), "dog definition missing");
    session.selectAnimal(engine, dog);
    require(session.spawn(engine, 1) == 1, "active profile protection scenario did not spawn");
    const auto* dogSurface = session.enemies()->enemies()[0].navigation;
    session.selectAnimal(engine, 0);
    require(!session.checkTower(engine, cached).empty(), "cached placement ignored a newly occupied large profile");
    session.reset(engine);
    require(session.checkTower(engine, cached).empty(), "removed animals left a stale placement rejection");
    const auto previewStart = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
        auto position = cached;
        position.position.x += (i % 21 - 10) * 0.01;
        require(session.checkTower(engine, position, false).empty(), "instant footprint check rejected clear terrain");
    }
    std::cout << "1000 tower hover checks: "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - previewStart).count()
              << " ms; no route baking requested.\n";
    editor.document().save(output);
    tick(engine, 3);
    auto* window = SDL_GetWindowFromID(windowId);
    const auto firstPoint = screenPoint(engine, window, {5.37, 0, 3.55});
    const auto initialRevision = session.navigation().revision();
    const auto gridRevision = engine.scene()->navigationGrid()->revision();
    click(engine, windowId, {130, 11});
    popupControl(engine, windowId, "Towers");
    popupControl(engine, windowId, "Place Tower");
    require(debug.capturesSelection() && session.towers().towers().empty(), "Place Tower control failed");
    mouse(windowId, SDL_EVENT_MOUSE_MOTION, firstPoint);
    tick(engine, 1);
    require(debug.boxes().size() == 1 && debug.boxes()[0].color.g > debug.boxes()[0].color.r &&
                !editor.document().dirty() && session.navigation().revision() == initialRevision,
            "valid transparent preview changed the scene or live NavMesh");
    for (const auto position : {glm::dvec3{5.35, 0, 3.53}, glm::dvec3{5.39, 0, 3.57}, glm::dvec3{5.37, 0, 3.55}}) {
        mouse(windowId, SDL_EVENT_MOUSE_MOTION, screenPoint(engine, window, position));
        tick(engine, 1);
        require(debug.boxes().size() == 1 && debug.boxes()[0].color.g > debug.boxes()[0].color.r,
                "moving the cursor restarted a preview delay");
    }
    click(engine, windowId, firstPoint);
    require(session.towers().towers().size() == 1 && editor.document().dirty(), "viewport click did not place a tower");
    const auto first = session.towers().towers()[0];
    require(std::abs(first.position.x - 5.37) < 0.002 && std::abs(first.position.z - 3.55) < 0.002 &&
                session.navigation().revision() > initialRevision &&
                engine.scene()->navigationGrid()->revision() == gridRevision,
            "tower snapped to cells or did not update navigation");
    require(debug.boxes().size() == 1 && debug.boxes()[0].color.r > debug.boxes()[0].color.g,
            "overlapping preview was not red");
    require(!dogSurface->ready(), "unused large-animal profile was eagerly rebuilt during tower placement");
    const auto closingPoint = screenPoint(engine, window, {5.5, 0, 4.55});
    mouse(windowId, SDL_EVENT_MOUSE_MOTION, closingPoint);
    tick(engine, 1);
    require(debug.boxes().size() == 1 && debug.boxes()[0].color.g > debug.boxes()[0].color.r,
            "clear footprint did not respond immediately");
    click(engine, windowId, closingPoint);
    require(session.towers().towers().size() == 1 && debug.boxes().size() == 1 &&
                debug.boxes()[0].color.r > debug.boxes()[0].color.g,
            "click-time route validation did not reject a sealing tower");
    const auto beforeZoom = engine.scene()->resolvedCamera();
    SDL_Event zoom{};
    zoom.type = SDL_EVENT_MOUSE_WHEEL;
    zoom.wheel.windowID = windowId;
    zoom.wheel.y = 1;
    require(SDL_PushEvent(&zoom), SDL_GetError());
    tick(engine);
    const auto afterZoom = engine.scene()->resolvedCamera();
    require(glm::length(afterZoom.position - afterZoom.target) < glm::length(beforeZoom.position - beforeZoom.target) &&
                debug.capturesSelection() && session.towers().towers().size() == 1,
            "placement mode captured camera input or placed a tower during zoom");
    engine.scene()->setResolvedCamera(beforeZoom);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    tick(engine);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    tick(engine);
    require(!debug.capturesSelection() && debug.boxes().empty(), "Escape did not cancel placement");
    session.selectAnimal(engine, dog);
    require(dogSurface->ready() && !dogSurface->contains({first.position.x, 0, first.position.z}),
            "on-demand profile rebuild did not include existing towers");
    session.selectAnimal(engine, 0);

    game::Tower closing;
    closing.position = {5.5, 0.75, 4.55};
    const auto beforeRejected = session.navigation().revision();
    require(!session.checkTower(engine, closing).empty() && session.navigation().revision() == beforeRejected,
            "last-route placement was accepted or mutated live navigation");
    bool rejected = false;
    try {
        (void)session.placeTower(engine, closing);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && session.towers().towers().size() == 1, "invalid placement created a scene object");
    require(!session.checkTower(engine, first).empty(), "tower overlap accepted");
    game::Tower wall;
    wall.position = {5.5, 0.75, 1.5};
    require(!session.checkTower(engine, wall).empty(), "wall overlap accepted");

    require(session.spawn(engine, 6) == 6, "tower scenario enemy spawn failed");
    session.update(engine, 0);
    const auto* surface = session.enemies()->enemies()[0].navigation;
    const auto oldPathRevision = session.enemies()->enemies()[0].path.revision;
    game::Tower second;
    second.position = {8.2, 0.75, 2.3};
    const auto secondId = session.placeTower(engine, second);
    require(session.enemies()->enemies()[0].navigation == surface && surface->revision() != oldPathRevision,
            "installing a tower invalidated a borrowed mesh address");
    require(engine.scene()->animation(first.object).asset() == engine.scene()->animation(secondId).asset(),
            "tower instances did not share the cube model");
    session.update(engine, 1.0 / 60);
    require(session.enemies()->enemies()[0].path.revision == surface->revision(), "enemy kept stale tower corridors");
    bool passed = false, reached = false;
    for (int frame = 0; frame < 600; ++frame) {
        require(engine.update([&](double) {
            session.update(engine, 1.0 / 60);
        }),
                "tower movement frame failed");
        for (const auto& enemy : session.enemies()->enemies()) {
            require(enemy.navigation->contains(enemy.position), "animal entered a tower obstacle");
            passed = passed || enemy.position.x > 6.5;
        }
        reached = reached || session.enemies()->metrics().arrived > 0;
    }
    require(passed && reached, "animals failed to navigate around towers and reach their goal");
    engine.clearSelection();
    click(engine, windowId, screenPoint(engine, window, first.position));
    require(engine.selectedObject() == first.object, "normal scene picking did not select the tower");
    editor.document().rename(first.object, "Renamed tower");
    editor.document().save(output);
    const auto saved = Scene::load(output);
    require(saved->object(first.object).role == "grimlock.tower", "tower identification depended on its name");
    const auto beforeRemoval = session.navigation().revision();
    editor.document().remove(first.object);
    session.update(engine, 0);
    require(session.towers().towers().size() == 1 && session.navigation().revision() != beforeRemoval &&
                session.navigation().contains({first.position.x, 0, first.position.z}),
            "editor deletion did not restore navigation");
    const auto afterRemoval = session.navigation().revision();
    session.update(engine, 0);
    require(session.navigation().revision() == afterRemoval, "unchanged towers rebuilt navigation every frame");
    editor.document().open(output);
    session.update(engine, 0);
    require(session.towers().towers().size() == 2 && session.enemies()->enemies().empty() &&
                !session.navigation().contains({first.position.x, 0, first.position.z}),
            "loading a scene did not restore tower obstacles or clear old agents");
    engine.scene()->selectObject(first.object);
    navigationControl(engine, windowId, "Towers", "Remove Selected Tower");
    require(session.towers().towers().size() == 1 && editor.document().dirty(), "tower removal control failed");
    require(session.removeTower(engine, secondId) && !session.removeTower(engine, secondId) &&
                session.towers().towers().empty(),
            "tower removal retained stale IDs");
    navigationControl(engine, windowId, "Towers", "Tower Type", "Large Tower");
    click(engine, windowId, {130, 11});
    popupControl(engine, windowId, "Towers");
    popupControl(engine, windowId, "Place Tower");
    const auto widePoint = screenPoint(engine, window, {8.3, 0, 2.3});
    mouse(windowId, SDL_EVENT_MOUSE_MOTION, widePoint);
    tick(engine, 1);
    require(!debug.boxes().empty() && debug.boxes()[0].color.g > debug.boxes()[0].color.r,
            "selected wide tower preview was not immediately valid");
    click(engine, windowId, widePoint);
    require(session.towers().towers().size() == 1, "selected tower type did not place through the viewport");
    const auto wide = session.towers().towers().back();
    require(wide.type == "wide" && wide.halfExtent == session.towers().definition("wide").halfExtent,
            "tower selection did not use its configured footprint");
    game::Tower basic;
    basic.position = {7.8, 0.75, 4.1};
    const auto basicId = session.placeTower(engine, basic);
    require(engine.scene()->animation(wide.object).asset() == engine.scene()->animation(basicId).asset(),
            "tower types duplicated shared model resources");
    auto editedVisual = engine.scene()->object(wide.object).local;
    editedVisual.rotation = glm::angleAxis(0.3F, glm::vec3{0, 1, 0});
    engine.scene()->setTransform(wide.object, editedVisual);
    session.synchronize(engine);
    editedVisual.scale *= 0.5F;
    engine.scene()->setTransform(wide.object, editedVisual);
    const auto physicalRevision = session.navigation().revision();
    session.synchronize(engine);
    require(session.towers().towers().front().halfExtent == wide.halfExtent &&
                session.navigation().revision() == physicalRevision,
            "visual scaling changed physical bounds or rebuilt navigation");
    editor.document().save(output);
    editor.document().open(output);
    session.synchronize(engine);
    require(session.placement().zones().size() == zoneCount && session.towers().towers().size() == 2 &&
                session.towers().towers().front().type == "wide" &&
                session.towers().towers().front().halfExtent == wide.halfExtent &&
                glm::length(session.towers().towers().front().rotation - glm::dquat{editedVisual.rotation}) < 1e-5 &&
                !session.navigation().contains({wide.position.x, 0, wide.position.z}),
            "loading lost zones, tower types, footprints or navigation obstacles");
}

void checkLevels(Engine& engine, game::NavigationDebug& debug, editor::Editor& editor, SDL_WindowID window) {
    std::cout << "Checking handcrafted level loading and waves..." << std::endl;
    // Save the preceding editor test so the normal unsaved-change dialog does not intercept this load.
    editor.document().save(editor.document().path());
    navigationControl(engine, window, "Level", "Load First Watch");
    auto& level = debug.level();
    auto& session = debug.session();
    require(level.waves() && level.waves()->phase() == game::LevelPhase::preparation &&
                level.waves()->health() == level.waves()->definition().objectiveHealth &&
                session.enemies()->enemies().empty() &&
                level.economy().gold() == level.waves()->definition().startingGold,
            "level load did not reset the scene, objective and enemies");
    // Keep editor transaction/wave controls independent of the full gameplay balance playthrough.
    auto fixture = level.waves()->definition();
    fixture.startingGold = 100;
    fixture.objectiveHealth = 30;
    fixture.waves = {{"Editor first wave", {{"Chicken", 0, 4, 2, 0, 2}}, 25},
                     {"Editor mixed wave", {{"Chicken", 0, 4, 2, 0, 1.5}, {"Black Rat", 1, 4, 2, 0, 2}}, 35},
                     {"Editor final wave", {{"Bunny", 1, 2, 2, 0, 0}, {"Chicken", 0, 4, 1, 4, 1}}, 50}};
    level.begin(engine, session, fixture);
    game::Tower tower;
    tower.position = {-3.3, 0.75, 0};
    const auto failsWithoutSpending = [&](const game::Tower& candidate) {
        const auto gold = level.economy().gold();
        const auto towers = session.towers().towers().size();
        bool rejected = false;
        try {
            (void)level.buildTower(engine, session, candidate);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && level.economy().gold() == gold && session.towers().towers().size() == towers,
                "failed construction spent gold or created a tower");
    };
    auto outside = tower;
    outside.position.x = -6;
    failsWithoutSpending(outside);
    ImGui::SetWindowPos("Level", {900, 35});
    ImGui::SetWindowFocus("Level");
    tick(engine);
    panelControl(engine, window, "Small Tower");
    panelControl(engine, window, "Build Tower");
    auto* native = SDL_GetWindowFromID(window);
    click(engine, window, screenPoint(engine, native, {-3.3, 0, 0}));
    require(session.towers().towers().size() == 1 && level.economy().gold() == 70,
            "gameplay UI construction did not charge the configured cost");
    failsWithoutSpending(tower);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    tick(engine);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    tick(engine);
    std::uint64_t expectedGold = 70;
    for (int wave = 0; wave < 3; ++wave) {
        require(level.waves()->phase() == game::LevelPhase::preparation, "wave skipped preparation");
        require(session.placementEnabled(), "preparation did not restore construction");
        level.startWave(session);
        require(!session.checkTower(engine, tower, false).empty(), "battle phase allowed construction");
        failsWithoutSpending(tower);
        for (int frame = 0; frame < 2400 && level.waves()->phase() == game::LevelPhase::battle; ++frame) {
            debug.update(engine, 1.0 / 30);
            for (const auto& enemy : session.enemies()->enemies()) {
                require(enemy.navigation->contains(enemy.position), "wave animal entered an obstacle");
            }
            if (frame % 60 == 0) {
                tick(engine, 1);
            }
        }
        require(level.waves()->phase() != game::LevelPhase::battle, "authored wave did not drain its enemies");
        const auto reward = level.waves()->definition().waves[wave].goldReward;
        expectedGold += reward;
        require(level.economy().gold() == expectedGold && level.lastWaveReward() == reward,
                "wave completion did not grant the exact fixed reward");
        debug.update(engine, 0);
        debug.update(engine, 0);
        require(level.economy().gold() == expectedGold, "wave reward was granted more than once");
    }
    const auto& combatMetrics = session.enemies()->metrics();
    require(level.waves()->phase() == game::LevelPhase::victory &&
                level.waves()->health() == 30 - combatMetrics.arrived && combatMetrics.defeated > 0 &&
                combatMetrics.arrived + combatMetrics.defeated == 18 && session.enemies()->enemies().empty(),
            "fixed level counts, arrival damage or victory incorrect");

    const auto definition = level.waves()->definition();
    editor.document().open(engine.assetRoot() / definition.scene);
    level.begin(engine, session, definition);
    require(session.towers().towers().empty() && session.enemies()->metrics().arrived == 0 &&
                level.waves()->wave() == 0 && level.waves()->health() == 30 && level.economy().gold() == 100 &&
                !level.lastWaveReward(),
            "level restart retained towers, arrivals or progress");

    auto poor = definition;
    poor.startingGold = 29;
    level.begin(engine, session, poor);
    failsWithoutSpending(tower);
    // The editor's authoring path remains free even while a gameplay level is active.
    const auto authored = session.placeTower(engine, tower, game::PlacementMode::authoring);
    require(level.economy().gold() == 29 && engine.scene()->contains(authored), "editor authoring spent gameplay gold");
    require(session.removeTower(engine, authored) && level.economy().gold() == 29, "removal refunded gold");

    auto exact = definition;
    exact.startingGold = session.towers().definition("wide").constructionCost;
    level.begin(engine, session, exact);
    auto wide = session.towers().definition("wide").instance();
    wide.position = {-3.3, wide.halfExtent.y, 0};
    const auto purchasedWide = level.buildTower(engine, session, wide);
    require(level.economy().gold() == 0, "exact-cost Wide tower purchase charged incorrectly");
    failsWithoutSpending(tower);
    require(session.removeTower(engine, purchasedWide) && level.economy().gold() == 0,
            "removing a purchased tower refunded currency");

    auto fragile = definition;
    fragile.objectiveHealth = 1;
    level.begin(engine, session, fragile);
    level.startWave(session);
    const auto battleAuthoring = session.placeTower(engine, tower, game::PlacementMode::authoring);
    require(level.economy().gold() == fragile.startingGold && session.removeTower(engine, battleAuthoring),
            "battle inspection prevented free editor authoring or charged currency");
    for (int frame = 0; frame < 2400 && level.waves()->phase() == game::LevelPhase::battle; ++frame) {
        debug.update(engine, 1.0 / 30);
    }
    require(level.waves()->phase() == game::LevelPhase::defeat && level.waves()->health() == 0,
            "arrival did not defeat a depleted objective");
    require(level.economy().gold() == definition.startingGold && !level.lastWaveReward(),
            "defeat granted a wave reward");
    const auto pending = level.waves()->pending();
    const auto active = session.enemies()->enemies().size();
    debug.update(engine, 60);
    require(level.waves()->pending() == pending && session.enemies()->enemies().size() == active,
            "defeat continued running the battle");
    editor.document().open(engine.assetRoot() / "scenes/demo.json");
    debug.update(engine, 0);
    require(!level.waves() && session.enemies()->enemies().empty(), "leaving level retained gameplay state");
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
        checkTowers(engine, debug, editor, id, output.parent_path() / "tower-runtime.json");
        checkLevels(engine, debug, editor, id);
        editor.document().open(game / "assets/scenes/hill_watch.json");
        debug.update(engine, 0);
        navigationControl(engine, id, "Level", "Load Hill Watch");
        tick(engine, 3);
        require(debug.level().waves() && debug.level().waves()->definition().objectiveHeight == 2 &&
                    !engine.scene()->navigationGrid() && debug.session().navigation().ready(),
                "Editor could not load the elevated level independently of a grid");
        navigationControl(engine, id, "Show NavMesh");
        require(!debug.triangles().empty(), "elevated NavMesh overlay was unavailable in Editor");
        navigationControl(engine, id, "Show NavMesh");
        const auto elevated = debug.session().towerAt({{8, 10, 2}, {0, -1, 0}, 0, 30});
        require(elevated && debug.session().checkTower(engine, *elevated, false).empty(),
                "Editor rejected an elevated tower preview");
        navigationControl(engine, id, "Level", "Load Cover Watch");
        tick(engine, 3);
        engine.scene()->selectObject(11);
        require(debug.lines().empty(), "combat debugging was enabled by default");
        navigationControl(engine, id, "Show Combat Visibility");
        tick(engine);
        require(debug.lines().size() >= 192, "Editor did not draw the selected tower's 3D attack sphere");
        debug.level().startWave(debug.session());
        debug.update(engine, 0);
        tick(engine);
        require(debug.lines().size() > 192, "Editor did not draw visibility rays to spawned targets");
        navigationControl(engine, id, "Show Combat Visibility");
        tick(engine);
        require(debug.lines().empty(), "combat debugging remained visible after disabling it");
        navigationControl(engine, id, "Level", "Load Rival Watch");
        tick(engine, 3);
        require(debug.level().waves() && debug.level().waves()->definition().waves.size() == 4 &&
                    debug.level().waves()->definition().waves.front().groups.front().enemy == "Grunt",
                "Editor could not load handcrafted archetype waves");
        std::cout << "PASS enemy controls, edit-only overlay, world destinations, predefined radii, resources and "
                     "animations.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL enemy runtime: " << error.what() << '\n';
        return 1;
    }
}
