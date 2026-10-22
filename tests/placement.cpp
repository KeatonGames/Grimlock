#include "entities/tower_definition.hpp"
#include "systems/placement_system.hpp"

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

void checkContainment() {
    BuildZone zone;
    zone.position = {4.13, 2, -3.27};
    zone.dimensions = {4, 2};
    Tower tower;
    tower.position = zone.position + glm::dvec3{0, tower.halfExtent.y, 0};
    require(zone.contains(tower), "continuous position/elevation rejected");
    tower.position.x += 1.55;
    require(zone.contains(tower), "exact footprint boundary rejected");
    tower.position.x += 0.0001;
    require(!zone.contains(tower), "center-only containment accepted overhang");
    tower.position = zone.position + glm::dvec3{0, tower.halfExtent.y, 0.5};
    require(zone.contains(tower), "unrotated tower should fit");
    tower.rotation = glm::angleAxis(glm::radians(45.0), glm::dvec3{0, 1, 0});
    require(!zone.contains(tower), "rotated corner outside zone accepted");
    zone.yaw = glm::radians(37.0);
    tower.rotation = glm::angleAxis(zone.yaw, glm::dvec3{0, 1, 0});
    tower.position = zone.position + tower.rotation * glm::dvec3{1.54, tower.halfExtent.y, 0.54};
    require(zone.contains(tower), "rotated zone/local footprint conversion incorrect");
    tower.position += tower.rotation * glm::dvec3{0.02, 0, 0};
    require(!zone.contains(tower), "rotated zone edge accepted overhang");
    tower.position = zone.position + glm::dvec3{0, tower.halfExtent.y + 1, 0};
    require(!zone.contains(tower), "wrong terrain elevation accepted");
    tower.position.y -= 1;
    tower.visualScale = {100, 100, 100};
    require(zone.contains(tower), "visual scale changed physical containment");
    zone.dimensions.x = 0;
    require(!zone.contains(tower), "zero-size zone accepted");
}

void checkPersistence(const std::filesystem::path& path) {
    Scene scene{"Construction test"};
    PlacementSystem placement;
    Tower tower;
    tower.position = {1.37, 0.75, -2.19};
    placement.synchronize(&scene);
    require(!placement.check(tower).empty(), "no-zone scene allowed construction");
    BuildZone zone;
    zone.name = "Renamable rectangle";
    zone.position = {1.37, 0, -2.19};
    zone.dimensions = {4, 3};
    zone.yaw = glm::radians(25.0);
    zone.object = PlacementSystem::createZone(scene, zone);
    placement.synchronize(&scene);
    require(placement.check(tower).empty(), "authored zone not recognized");
    zone.name = "Edited zone";
    zone.dimensions = {5, 4};
    PlacementSystem::editZone(scene, zone);
    const auto id = scene.createObject("Typed tower");
    scene.setRole(id, "grimlock.tower:wide");
    scene.setModel(id, "models/cube.obj");
    Transform transform;
    transform.position = tower.position;
    transform.rotation = glm::angleAxis(0.3F, glm::vec3{0, 1, 0});
    transform.scale = {0.1F, 2, 0.2F};
    scene.setTransform(id, transform);
    scene.save(path);
    const auto loaded = Scene::load(path);
    placement.synchronize(loaded.get());
    require(placement.zones().size() == 1 && placement.zones()[0].name == zone.name &&
                glm::length(placement.zones()[0].position - zone.position) < 1e-5 &&
                glm::length(placement.zones()[0].dimensions - zone.dimensions) < 1e-5 &&
                std::abs(placement.zones()[0].yaw - zone.yaw) < 1e-5,
            "zone name, transform or dimensions did not round trip");
    require(loaded->object(id).role == "grimlock.tower:wide" && loaded->object(id).local.scale == transform.scale &&
                glm::length(loaded->object(id).local.rotation - transform.rotation) < 1e-6,
            "tower type, visual scale or rotation did not round trip");
    loaded->setEnabled(zone.object, false);
    placement.synchronize(loaded.get());
    require(!placement.check(tower).empty(), "disabled zone allowed construction");
    loaded->setEnabled(zone.object, true);
    transform.rotation = glm::angleAxis(0.3F, glm::vec3{1, 0, 0});
    loaded->setTransform(zone.object, transform);
    placement.synchronize(loaded.get());
    require(placement.zones().empty() && !placement.error().empty(), "tilted zone silently accepted");
    loaded->removeObject(zone.object);
    placement.synchronize(loaded.get());
    require(placement.zones().empty(), "deleted zone retained construction space");

    Scene adjacent;
    zone.position = {-1, 0, 0};
    zone.yaw = 0;
    zone.dimensions = {2, 2};
    (void)PlacementSystem::createZone(adjacent, zone);
    zone.position.x = 1;
    (void)PlacementSystem::createZone(adjacent, zone);
    placement.synchronize(&adjacent);
    tower.position = {0, 0.75, 0};
    require(!placement.check(tower).empty(), "footprint spanning two independent zones accepted");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected asset root and fixture path");
        const auto definitions = loadTowerDefinitions(argv[1]);
        require(definitions.size() == 2 && definitions[0].id == "basic" &&
                    definitions[0].halfExtent != definitions[1].halfExtent &&
                    definitions[1].instance().halfExtent != definitions[1].instance().visualScale,
                "tower types/independent footprint configuration incorrect");
        checkContainment();
        checkPersistence(argv[2]);
        std::cout << "PASS rotated zones, complete footprints, elevation, editing, persistence, disabled/deleted zones "
                     "and types.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL placement: " << error.what() << '\n';
        return 1;
    }
}
