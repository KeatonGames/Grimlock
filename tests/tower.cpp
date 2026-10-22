#include "systems/tower_system.hpp"

#include <cmath>
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

void checkFootprintsAndRays() {
    Grid grid{{8, 8, 1, {0, 0, 0}}};
    auto terrain = terrainBoxes(grid);
    TowerSystem towers;
    Tower tower;
    tower.position = {2.37, 0.75, 2.19};
    require(towers.check(tower, terrain, {}).empty(), "continuous supported placement rejected");
    auto rotated = tower;
    rotated.rotation = glm::angleAxis(0.7, glm::dvec3{0, 1, 0});
    require(towers.check(rotated, terrain, {}).empty() && rotated.bounds().halfExtent.x > 0.45F,
            "rotation did not conservatively rotate the footprint");
    auto outside = tower;
    outside.position.x = 0.2;
    require(!towers.check(outside, terrain, {}).empty(), "unsupported edge footprint accepted");
    grid.setWalkable({2, 2}, false);
    terrain = terrainBoxes(grid);
    require(!towers.check(tower, terrain, {}).empty(), "wall overlap accepted");
    const auto hit = TowerSystem::terrainHit({{2.37, 10, 2.19}, {0, -1, 0}, 0, 20}, terrain);
    require(hit && std::abs(hit->y - 1.5) < 1e-5, "ray ignored the nearest wall surface");
    grid.setWalkable({2, 2}, true);
    terrain = terrainBoxes(grid);
    const auto floor = TowerSystem::terrainHit({{2.37, 10, 2.19}, {0, -1, 0}, 0, 20}, terrain);
    require(floor && glm::length(*floor - glm::dvec3{2.37, 0, 2.19}) < 1e-5, "ray snapped to grid centers");
    require(!TowerSystem::terrainHit({{20, 10, 20}, {0, -1, 0}, 0, 20}, terrain), "off-terrain ray hit");
    require(!TowerSystem::terrainHit({{0, 0, 0}, {0, 0, 0}, 0, 20}, terrain), "zero ray direction accepted");
    std::array enemies{Enemy{}};
    enemies[0].position = {2.37, 0, 2.19};
    require(!towers.check(tower, terrain, enemies).empty(), "tower could trap an animal");
    Grid shifted{{4, 4, 2.5, {10, 3, -7}}};
    const auto elevatedTerrain = terrainBoxes(shifted);
    tower.position = {11.13, 3.75, -5.16};
    require(towers.check(tower, elevatedTerrain, {}).empty(), "world origin, elevation or cell size changed placement");
    const auto elevated = TowerSystem::terrainHit({{11.13, 10, -5.16}, {0, -1, 0}, 0, 20}, elevatedTerrain);
    require(elevated && std::abs(elevated->y - 3) < 1e-5, "terrain ray ignored world elevation");
}

void checkContinuousSpawnRoutes() {
    NavigationGeometry geometry;
    geometry.addBox({0, -0.2F, 0}, {12, 0, 8}, true);
    geometry.addBox({5, 0, 0}, {6, 2, 3}, false);
    geometry.addBox({5, 0, 5}, {6, 2, 8}, false);
    NavMesh current;
    require(current.build(geometry), "route map failed");
    const SpawnArea area{{1.1, 3.13}, {1.15, 3.16}, 0};
    require(TowerSystem::routeFromArea(current, area, {10, 0, 4}), "small continuous spawn region was missed");
    require(TowerSystem::routeFromArea(current, {{1.13, 3.15}, {1.13, 3.15}, 0}, {10, 0, 4}),
            "point spawn region was missed");
    const auto revision = current.revision();
    NavigationPath oldPath;
    require(current.findPath({1.13, 0, 3.15}, {10, 0, 4}, oldPath), "original route missing");
    geometry.addBox({4.92F, 0, 3.1F}, {5.82F, 1.5F, 4.0F}, false);
    NavMesh first;
    require(first.build(geometry) && TowerSystem::routeFromArea(first, area, {10, 0, 4}),
            "first tower closed the only corridor");
    current.replaceWith(std::move(first));
    require(current.revision() != revision && !current.steering({1.13, 0, 3.15}, oldPath),
            "candidate installation did not invalidate old corridors");
    geometry.addBox({5.05F, 0, 4.1F}, {5.95F, 1.5F, 5.0F}, false);
    NavMesh sealed;
    require(sealed.build(geometry) && !TowerSystem::routeFromArea(sealed, area, {10, 0, 4}),
            "last-route closure was not detected");
    require(TowerSystem::routeFromArea(current, area, {10, 0, 4}), "trial validation modified live navigation");
    require(!TowerSystem::routeFromArea(current, {{30, 30}, {31, 31}, 0}, {10, 0, 4}), "off-mesh spawn accepted");
}

void checkRoleSerialization(const std::filesystem::path& path) {
    Scene scene;
    const auto id = scene.createObject("Renamable tower");
    scene.setRole(id, "grimlock.tower");
    scene.setModel(id, "models/cube.obj");
    Transform transform;
    transform.position = {2.37F, 0.75F, 3.19F};
    transform.scale = {0.45F, 0.75F, 0.45F};
    scene.setTransform(id, transform);
    scene.save(path);
    const auto loaded = Scene::load(path);
    require(loaded->object(id).role == "grimlock.tower" && loaded->object(id).local.position == transform.position,
            "tower role/continuous transform did not round trip");
}

void checkDebugScene(const std::filesystem::path& path) {
    const auto scene = Scene::load(path);
    require(scene->navigationGrid(), "debug scene has no terrain");
    const auto& grid = *scene->navigationGrid();
    const auto& configuration = grid.configuration();
    const SpawnArea spawn{{-11.5, -8.5}, {-4.5, 8.5}, 0};
    const glm::dvec3 destination{11.5, 0, 8.5};
    auto geometry = navigationGeometry(terrainBoxes(grid));
    for (const auto& [id, object] : scene->objects()) {
        if (object.role != "grimlock.tower") {
            continue;
        }
        Tower tower;
        tower.position = scene->worldTransform(id)[3];
        const auto bounds = tower.bounds();
        geometry.addBox(bounds.center - bounds.halfExtent, bounds.center + bounds.halfExtent, false);
    }
    const auto definitions = loadAnimalDefinitions(path.parent_path().parent_path());
    for (const auto& definition : definitions) {
        NavMesh navigation;
        NavMeshConfiguration settings;
        settings.agentRadius = static_cast<float>(definition.radius);
        settings.agentHeight = static_cast<float>(definition.height);
        require(navigation.build(geometry, settings), navigation.status().c_str());
        require(TowerSystem::routeFromArea(navigation, spawn, destination),
                (definition.name + " has no default route through the debug scene").c_str());
        require(!navigation.contains({0.8, configuration.origin.y, -0.7}), "debug tower has no navigation footprint");
        if (definition.model == "models/chicken.glb") {
            NavigationPath route;
            require(navigation.contains({9.5, 0, -6.5}) && !navigation.findPath({9.5, 0, -6.5}, destination, route),
                    "debug pocket is not a navigable isolated region");
            require(navigation.contains({5.5, 0, 6.5}), "small-animal shortcut is not usable by chickens");
        } else if (definition.radius > 0.5) {
            require(!navigation.contains({5.5, 0, 6.5}), "large animal fits the single-cell debug shortcut");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected fixture and debug scene paths");
        checkFootprintsAndRays();
        checkContinuousSpawnRoutes();
        checkRoleSerialization(argv[1]);
        checkDebugScene(argv[2]);
        std::cout << "PASS tower footprints, rays, continuous spawn regions, route closure and scene roles.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL towers: " << error.what() << '\n';
        return 1;
    }
}
