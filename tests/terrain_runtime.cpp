#include "navigation/level_geometry.hpp"
#include "runtime/level_runtime.hpp"
#include "systems/melee_navigation.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace grimlock;
using namespace grimlock::game;

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void layers() {
    NavigationGeometry geometry;
    geometry.addBox({-5, -0.5F, -5}, {5, 0, 5}, true);
    geometry.addBox({-4, 2.5F, -4}, {4, 3, 4}, true);
    NavMesh mesh;
    require(mesh.build(geometry), mesh.status().c_str());
    NavigationPath path;
    require(mesh.contains({0, 0, 0}) && mesh.contains({0, 3, 0}), "stacked floors lost their elevation");
    require(!mesh.visible({0, 0, 0}, {0, 3, 0}), "Detour X/Z raycast saw through disconnected floors");
    require(!mesh.findPath({0, 0, 0}, {0, 3, 0}, path), "path jumped between stacked floors");
    require(!meleeContact(mesh, {0, 0, 0}, 0.2, {{0, 3, 0}, 0.2}, 4), "melee hit through a ceiling");
    Enemy first, second;
    first.position = {0, 0, 0};
    second.position = {0, 3, 0};
    first.id = 1;
    second.id = 2;
    first.destinationAssigned = second.destinationAssigned = true;
    std::array<Enemy, 2> enemies{first, second};
    CrowdMovement crowd;
    crowd.update(mesh, enemies, 1.0 / 30, false);
    require(glm::length(enemies[0].position - first.position) < 0.001 &&
                glm::length(enemies[1].position - second.position) < 0.001,
            "crowd separation pushed animals on different floors");

    NavigationGeometry stairs;
    stairs.addBox({-4, -0.5F, -2}, {0, 0, 2}, true);
    stairs.addBox({0, -0.5F, -2}, {2, 0.1F, 2}, true);
    stairs.addBox({2, -0.5F, -2}, {6, 0.2F, 2}, true);
    require(mesh.build(stairs) && mesh.findPath({-2, 0, 0}, {4, 0.2, 0}, path), "valid stairs were disconnected");
    auto settings = mesh.configuration();
    settings.stepHeight = 0;
    require(mesh.build(stairs, settings) && !mesh.findPath({-2, 0, 0}, {4, 0.2, 0}, path),
            "stairs ignored the configured step height");

    NavigationGeometry ceiling;
    ceiling.addBox({-4, -0.5F, -4}, {4, 0, 4}, true);
    ceiling.addBox({-4, 0.8F, -4}, {4, 1, 4}, true);
    settings = {};
    settings.agentHeight = 1.4F;
    require(mesh.build(ceiling, settings) && !mesh.contains({0, 0, 0}) && mesh.contains({0, 1, 0}),
            "low overhead clearance ignored agent height");

    NavigationGeometry ledge;
    ledge.addBox({-4, -0.5F, -3}, {0, 0, 3}, true);
    ledge.addBox({0, 0.2F, -3}, {4, 0.7F, 3}, true);
    settings.agentRadius = 0.28F;
    require(mesh.build(ledge, settings) && !meleeContact(mesh, {-0.6, 0.05, 0}, 0.28, {{0.35, 0.75, 0}, 0.16}, 1.2),
            "melee crossed a short inaccessible ledge within nominal attack range");
}

void footprints(Engine& engine) {
    const auto file = engine.assetRoot() / "models/terrain_support_fixture.obj";
    {
        std::ofstream output{file};
        output << "v -1 0 -1\nv 0 0 -1\nv 0 0 1\nv -1 0 1\n"
                  "f 1 4 3\nf 1 3 2\nf 1 4 3\nf 1 3 2\n";
        require(static_cast<bool>(output), "could not write support fixture");
    }
    auto scene = std::make_unique<Scene>("Footprint coverage fixture");
    const auto first = scene->createObject("Duplicated half-floor triangles");
    scene->setModel(first, "models/terrain_support_fixture.obj");
    scene->setRole(first, "grimlock.terrain");
    engine.setScene(std::move(scene));
    LevelGeometry geometry;
    geometry.synchronize(engine);
    Tower tower;
    tower.position = {0, tower.halfExtent.y, 0};
    require(!geometry.supportError(tower).empty(), "overlapping triangles concealed an unsupported footprint");
    const auto second = engine.scene()->createObject("Other half of the foundation");
    engine.scene()->setModel(second, "models/terrain_support_fixture.obj");
    engine.scene()->setRole(second, "grimlock.terrain");
    Transform transform;
    transform.position.x = 1;
    engine.scene()->setTransform(second, transform);
    geometry.synchronize(engine);
    require(geometry.supportError(tower).empty(), "adjacent terrain sections could not support a footprint");
    const auto obstacle = engine.scene()->createObject("Enclosing obstacle");
    engine.scene()->setModel(obstacle, "models/cube.obj");
    engine.scene()->setRole(obstacle, "grimlock.obstacle");
    transform.position = {0, 1, 0};
    transform.scale = {2, 1, 2};
    engine.scene()->setTransform(obstacle, transform);
    geometry.synchronize(engine);
    require(!geometry.supportError(tower).empty(), "placement accepted a tower inside an obstacle volume");
    engine.scene()->setEnabled(obstacle, false);
    geometry.synchronize(engine);
    require(geometry.supportError(tower).empty(), "disabled obstacle retained placement references");
    std::filesystem::remove(file);
}

void terrain(Engine& engine) {
    engine.setScene(engine.loadScene(engine.assetRoot() / "scenes/hill_watch.json"));
    require(!engine.scene()->navigationGrid(), "test map still depends on a grid");
    LevelGeometry geometry;
    geometry.synchronize(engine);
    require(geometry.imported(), "terrain role did not import GLB triangles");
    const Ray down{{8, 12, 0}, {0, -1, 0}, 0, 30};
    const auto hit = geometry.intersect(down);
    require(hit && std::abs(hit->y - 2) < 1e-6, "terrain raycast did not hit the hill");
    const auto ramp = geometry.intersect({{4, 12, 0}, {0, -1, 0}, 0, 30});
    require(ramp && std::abs(ramp->y - 1) < 1e-6, "raycast replaced a ramp with its bounding box");
    const auto revision = geometry.revision();
    geometry.synchronize(engine);
    require(geometry.revision() == revision, "unchanged terrain invalidated geometry");
    auto transform = engine.scene()->object(1).local;
    transform.position.y = 1;
    engine.scene()->setTransform(1, transform);
    geometry.synchronize(engine);
    const auto moved = geometry.intersect(down);
    require(geometry.revision() != revision && moved && std::abs(moved->y - 3) < 1e-6,
            "terrain edits did not rebuild transformed geometry");
    transform.position.y = 0;
    engine.scene()->setTransform(1, transform);
    geometry.synchronize(engine);

    NavigationSession navigation;
    navigation.synchronize(engine);
    navigation.setDestination({{10, 0}});
    navigation.setDestinationHeight(2);
    navigation.spawnArea() = {{-7, -1}, {-5, 1}, 0};
    const auto& mesh = navigation.navigation();
    const auto meshRevision = mesh.revision();
    engine.scene()->setNavigationGrid(GridConfiguration{2, 2, 1, {0, -10, 0}});
    navigation.synchronize(engine);
    (void)engine.scene()->navigationGrid()->setWalkable({0, 0}, false);
    navigation.synchronize(engine);
    engine.scene()->setNavigationGrid(std::nullopt);
    navigation.synchronize(engine);
    require(mesh.revision() == meshRevision, "debug grid changes rebuilt imported terrain navigation");
    NavigationPath path;
    require(mesh.findPath({-6, 0, 0}, {10, 2, 0}, path), "ramp did not connect lower and upper surfaces");
    require(!mesh.findPath({-6, 0, 0}, {-2, 3, 11}, path), "agents jumped to an isolated overlook");
    require(meleeContact(mesh, {4, 1.05, 0}, 0.16, {{4.8, 1.45, 0}, 0.16}, 0.8),
            "valid melee contact on a connected ramp was rejected");
    require(!meleeContact(mesh, {1.5, 0, 4}, 0.16, {{6.3, 2, 4}, 0.16}, 6), "melee crossed an inaccessible cliff gap");
    auto restrictive = mesh.configuration();
    restrictive.maximumSlope = 10;
    NavMesh steep;
    require(steep.build(geometry.navigation(), restrictive) && !steep.findPath({-6, 0, 0}, {10, 2, 0}, path),
            "maximum slope did not exclude a steep connection");

    auto tower = navigation.towerAt({{7.7, 10, 1.2}, {0, -1, 0}, 0, 30}, "wide");
    require(tower && std::abs(tower->position.y - tower->halfExtent.y - 2) < 1e-6,
            "tower preview ignored terrain height");
    const auto placementError = navigation.checkTower(engine, *tower, false);
    require(placementError.empty(), placementError.c_str());
    auto unsupported = *tower;
    unsupported.position.x = 6.1;
    require(!geometry.supportError(unsupported).empty(), "unsupported footprint crossed a cliff");
    auto floating = *tower;
    floating.position.y += 0.3;
    require(!geometry.supportError(floating).empty(), "floating tower had terrain support");
    auto slope = *tower;
    slope.position = {4, 1 + slope.halfExtent.y, 0};
    require(!geometry.supportError(slope).empty(), "steep ramp accepted an upright tower foundation");
    const auto object = navigation.placeTower(engine, *tower);
    require(!navigation.navigation().contains({7.7, 2, 1.2}), "hill tower did not obstruct navigation");
    require(navigation.removeTower(engine, object) && navigation.navigation().contains({7.7, 2, 1.2}),
            "removing a hill tower did not restore navigation");

    HeroSystem heroes{engine};
    heroes.synchronize(navigation.towers());
    const auto hero = heroes.deploy({-2, 0, 0});
    require(heroes.command(hero, {9, 2, 0}), "hero could not navigate up the ramp");
    bool climbed = false;
    for (int i = 0; i < 900; ++i) {
        heroes.update(1.0 / 60, true);
        const auto* value = heroes.find(hero);
        climbed = climbed || (value->position.y > 0.5 && value->position.y < 1.7);
        require(heroes.navigation().contains(value->position), "hero left the sloping surface");
    }
    require(climbed && heroes.find(hero)->position.y > 1.9 && heroes.find(hero)->state == HeroState::reached,
            "hero did not arrive at the correct hill height");

    for (const double delta : {1.0 / 120, 1.0 / 30, 0.1}) {
        navigation.reset(engine);
        require(navigation.spawn(engine, 4) == 4, "lower terrain animal spawn failed");
        bool animalsClimbed = false;
        for (int frame = 0; frame < static_cast<int>(35 / delta); ++frame) {
            navigation.update(engine, delta);
            for (const auto& enemy : navigation.enemies()->enemies()) {
                require(mesh.contains(enemy.position), "animal left its NavMesh on a slope");
                animalsClimbed = animalsClimbed || enemy.position.y > 1;
            }
        }
        require(animalsClimbed && navigation.enemies()->metrics().arrived == 4,
                "animals stalled or arrived on the wrong elevation");
    }

    EnemyDefinition definition;
    definition.speed = 0;
    definition.despawnOnArrival = false;
    EnemySystem targets{engine, definition};
    require(targets.spawn(mesh, {{4, 0}, {4, 0}, 1}, 1) == 1, "lower combat target spawn failed");
    TowerSystem towers;
    towers.synchronize(engine);
    auto high = towers.definition("wide").instance();
    high.position = {4, 9, 0};
    const auto tooHigh = towers.create(engine, high);
    CombatSystem combat{engine};
    combat.update(towers, targets, 1);
    require(combat.shotsFired() == 0, "tower attack range still ignores vertical distance");
    require(towers.remove(engine, tooHigh), "combat fixture cleanup failed");
    (void)towers.create(engine, *tower);
    for (int i = 0; i < 60 && !targets.enemies().empty(); ++i) {
        combat.update(towers, targets, 0.1);
    }
    require(targets.metrics().defeated == 1 && combat.projectiles().impacts() >= 2,
            "hill tower projectiles did not hit and kill a lower target");
    require(engine.update(), "elevated scene failed to render");
}

void serialization(Engine& engine) {
    LevelRuntime runtime;
    runtime.load(engine, "assets/levels/hill_watch.json");
    const auto saved = engine.assetRoot().parent_path() / "hill-roundtrip.json";
    engine.saveScene(saved);
    const auto scene = engine.loadScene(saved);
    require(!scene->navigationGrid() && scene->object(1).role == "grimlock.terrain" &&
                std::abs(scene->object(101).local.position.y - 2) < 1e-6,
            "terrain/build-zone elevation did not survive scene serialization");
    const auto level = runtime.level().waves()->definition();
    const auto levelPath = engine.assetRoot().parent_path() / "hill-level-roundtrip.json";
    saveLevelDefinition(levelPath, level);
    require(loadLevelDefinition(levelPath).objectiveHeight == 2, "objective height did not survive serialization");
    runtime.startWave();
    for (int i = 0; i < 1200 && runtime.level().waves()->phase() == LevelPhase::battle; ++i) {
        runtime.update(engine, 0.1);
    }
    require(runtime.level().waves()->phase() == LevelPhase::preparation,
            "elevated wave could not reach its objective and complete");
    runtime.restart(engine);
    require(runtime.level().waves()->definition().objectiveHeight == 2 && runtime.level().economy().gold() == 150,
            "elevated level restart lost authored state");
}
} // namespace

int main() {
    try {
        layers();
        Engine engine{EngineConfig{.windowTitle = "Elevated terrain checks", .width = 640, .height = 480}};
        footprints(engine);
        terrain(engine);
        serialization(engine);
        std::cout << "Elevated GLB terrain, layers, movement, placement, combat and serialization passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
