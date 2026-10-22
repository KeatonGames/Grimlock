#include "systems/crowd_movement.hpp"

#include <chrono>
#include <cmath>
#include <glm/geometric.hpp>
#include <grimlock/terrain.hpp>
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

NavigationGeometry openFloor() {
    NavigationGeometry geometry;
    geometry.addBox({-5, -0.2F, -5}, {40, 0, 35}, true);
    return geometry;
}

std::vector<Enemy> makeEnemies(std::size_t count) {
    std::vector<Enemy> enemies;
    enemies.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        Enemy enemy;
        enemy.despawnOnArrival = true; // Keep the exit clear while measuring crowd throughput.
        enemy.id = i + 1;
        enemy.position = {1 + static_cast<double>(i % 20) * 0.55, 0, 1 + static_cast<double>(i / 20) * 0.55};
        enemy.radius = i % 2 ? EnemyDefinition{}.radius : 0.18;
        enemies.push_back(std::move(enemy));
    }
    return enemies;
}

void checkClearance(const NavMesh& navigation, std::span<const Enemy> enemies) {
    for (std::size_t i = 0; i < enemies.size(); ++i) {
        if (!enemies[i].active) {
            continue;
        }
        const auto& surface = enemies[i].navigation ? *enemies[i].navigation : navigation;
        require(surface.contains(enemies[i].position), "separation pushed an enemy outside NavMesh");
        require(std::isfinite(enemies[i].yaw), "invalid orientation");
        for (std::size_t j = i + 1; j < enemies.size(); ++j) {
            if (enemies[j].active) {
                require(glm::length(enemies[i].position - enemies[j].position) + 2e-5 >=
                            enemies[i].radius + enemies[j].radius,
                        "persistent enemy overlap");
            }
        }
    }
}

void checkDirectMovement() {
    NavMesh navigation;
    require(navigation.build(openFloor()), navigation.status().c_str());
    const glm::dvec2 destination{25.37, 13.83};
    glm::dvec3 reference{0};
    for (const int fps : {15, 30, 60, 144}) {
        auto enemies = makeEnemies(1);
        auto& enemy = enemies[0];
        enemy.position = {1.3, 0, -2.1};
        const auto start = enemy.position;
        const auto direction = glm::normalize(glm::dvec2{destination.x - start.x, destination.y - start.z});
        CrowdMovement movement;
        for (int frame = 0; frame < fps * 4; ++frame) {
            movement.update(navigation, destination, enemies, 1.0 / fps);
            const glm::dvec2 offset{enemy.position.x - start.x, enemy.position.z - start.z};
            require(glm::length(offset - direction * glm::dot(offset, direction)) < 1e-3,
                    "open route was not straight at an arbitrary angle");
        }
        if (fps == 15) {
            reference = enemy.position;
        }
        require(glm::length(enemy.position - reference) < 0.05, "movement depends excessively on frame rate");
        const double speed = glm::length(enemy.velocity);
        enemy.speed = 0;
        movement.update(navigation, destination, enemies, 1.0 / 60);
        require(glm::length(enemy.velocity) < speed && glm::length(enemy.velocity) > 0, "braking was not smooth");
        for (int frame = 0; frame < 60; ++frame) {
            movement.update(navigation, destination, enemies, 1.0 / 60);
        }
        require(glm::length(enemy.velocity) < 1e-6, "enemy did not stop after braking");
        enemy.speed = 1.5;
        for (int frame = 0; frame < fps * 30 && enemy.active; ++frame) {
            movement.update(navigation, destination, enemies, 1.0 / fps);
        }
        require(!enemy.active && enemy.movement == MovementStatus::arrived &&
                    glm::length(glm::dvec2{enemy.position.x, enemy.position.z} - destination) <= enemy.radius + 1e-4,
                "enemy did not arrive at the world destination");
    }
    require(navigation.statistics().pathQueries == 4, "open routes recalculated complete paths every frame");
}

void checkEditsAndInvalidQueries() {
    auto geometry = openFloor();
    geometry.addBox({14, 0, -5}, {15, 2, 35}, false);
    NavMesh navigation;
    require(navigation.build(geometry), navigation.status().c_str());
    auto enemies = makeEnemies(1);
    CrowdMovement movement;
    for (int frame = 0; frame < 180; ++frame) {
        movement.update(navigation, glm::dvec2{25, 5}, enemies, 1.0 / 60);
    }
    require(enemies[0].movement == MovementStatus::blocked && enemies[0].position.x == 1 &&
                navigation.statistics().pathQueries >= 2 && navigation.statistics().pathQueries <= 4,
            "unreachable route moved or retried excessively");
    require(navigation.build(openFloor()), "wall removal rebuild failed");
    movement.update(navigation, glm::dvec2{25, 5}, enemies, 0.25);
    require(enemies[0].position.x > 1 && enemies[0].path.revision == navigation.revision(),
            "rebuild did not replace an invalid corridor");
    const auto before = enemies[0].position;
    movement.update(navigation, glm::dvec2{1000, 1000}, enemies, 0.1);
    require(enemies[0].position == before && enemies[0].movement == MovementStatus::blocked,
            "invalid destination did not stop safely");
    movement.update(navigation, std::nullopt, enemies, 0.1);
    require(enemies[0].movement == MovementStatus::idle && enemies[0].position == before,
            "clearing destination did not stop movement");
    navigation.reset();
    movement.update(navigation, glm::dvec2{25, 5}, enemies, 0.1);
    require(enemies[0].position == before, "destroyed navigation data allowed movement");
}

void checkCrowd(std::size_t count, bool corridor) {
    auto geometry = openFloor();
    if (corridor) {
        geometry.addBox({14, 0, -5}, {15, 2, 14}, false);
        geometry.addBox({14, 0, 17}, {15, 2, 35}, false);
    }
    NavMesh navigation;
    require(navigation.build(geometry), navigation.status().c_str());
    auto enemies = makeEnemies(count);
    CrowdMovement movement;
    const auto start = std::chrono::steady_clock::now();
    int frames = 0;
    for (; frames < 60 * 180; ++frames) {
        movement.update(navigation, glm::dvec2{28.37, 15.29}, enemies, 1.0 / 60);
        if (frames % 60 == 0) {
            checkClearance(navigation, enemies);
        }
        bool active = false;
        for (const auto& enemy : enemies) {
            active = active || enemy.active;
        }
        if (!active) {
            break;
        }
    }
    checkClearance(navigation, enemies);
    std::size_t remaining = 0;
    for (const auto& enemy : enemies) {
        remaining += enemy.active ? 1 : 0;
    }
    std::cout << count << (corridor ? " corridor" : " open") << ": " << frames / 60.0 << " simulated seconds, "
              << remaining << " remaining, " << navigation.statistics().pathQueries << " path queries; CPU "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() /
                     (frames + 1)
              << " ms/update.\n";
    require(remaining == 0, "crowd failed to arrive without avoidable corridor deadlock");
    require(navigation.statistics().pathQueries < count * 10, "crowd performed excessive path searches");
}

void checkRecoveryAndSpikes() {
    NavMesh navigation;
    require(navigation.build(openFloor()), "recovery floor build failed");
    auto enemies = makeEnemies(2);
    enemies[1].position = enemies[0].position;
    CrowdMovement movement;
    for (int frame = 0; frame < 10; ++frame) {
        movement.update(navigation, std::nullopt, enemies, 1.0 / 60);
    }
    checkClearance(navigation, enemies);
    const auto before = enemies[0].position;
    movement.update(navigation, glm::dvec2{25, 10}, enemies, 100);
    require(glm::length(enemies[0].position - before) <= 0.4 && movement.metrics().substeps <= 64,
            "stalled frame caused an unbounded movement jump");
    checkClearance(navigation, enemies);
}

void checkCompactSpacing() {
    NavMesh navigation;
    NavMeshConfiguration config;
    config.agentRadius = static_cast<float>(EnemyDefinition{}.radius);
    require(navigation.build(openFloor(), config), "compact floor build failed");
    auto enemies = makeEnemies(2);
    for (auto& enemy : enemies) {
        enemy.radius = EnemyDefinition{}.radius;
        enemy.position = {0, 0, (enemy.id - 1) * 0.398};
    }
    CrowdMovement movement;
    for (int frame = 0; frame < 300; ++frame) {
        movement.update(navigation, glm::dvec2{25, 0.199}, enemies, 1.0 / 60);
        checkClearance(navigation, enemies);
        require(glm::length(enemies[0].position - enemies[1].position) < 0.405,
                "comfort separation spread a compact group unnecessarily");
    }
}

void checkMixedNavigation() {
    NavigationGeometry geometry;
    geometry.addBox({0, -0.2F, 0}, {10, 0, 8}, true);
    geometry.addBox({4, 0, 0}, {5, 2, 3.6F}, false);
    geometry.addBox({4, 0, 4.4F}, {5, 2, 8}, false);
    NavMesh small, large;
    NavMeshConfiguration config;
    config.agentRadius = 0.195F;
    require(small.build(geometry, config), small.status().c_str());
    config.agentRadius = 0.55F;
    config.agentHeight = 1;
    require(large.build(geometry, config), large.status().c_str());
    auto enemies = makeEnemies(2);
    enemies[0].radius = 0.195;
    enemies[0].position = {2, 0, 4};
    enemies[0].navigation = &small;
    enemies[1].radius = 0.55;
    enemies[1].position = {2, 0, 6};
    enemies[1].navigation = &large;
    CrowdMovement movement;
    for (int frame = 0; frame < 600; ++frame) {
        movement.update(small, glm::dvec2{8, 4}, enemies, 1.0 / 60);
        checkClearance(small, enemies);
    }
    require(!enemies[0].active && enemies[1].movement == MovementStatus::blocked && enemies[1].position.x == 2,
            "mixed radii did not use their own valid navigation clearance");
    require(small.statistics().pathQueries == 1 && large.statistics().pathQueries <= 11,
            "mixed navigation searched every frame");
}

void checkChickenMap(const std::filesystem::path& scenePath) {
    constexpr std::size_t count = 100;
    const auto scene = Scene::load(scenePath);
    const auto& grid = *scene->navigationGrid();
    NavMesh navigation;
    NavMeshConfiguration configuration;
    configuration.agentRadius = static_cast<float>(EnemyDefinition{}.radius);
    configuration.agentHeight = static_cast<float>(EnemyDefinition{}.height);
    require(navigation.build(navigationGeometry(terrainBoxes(grid)), configuration), navigation.status().c_str());
    const auto& c = grid.configuration();
    std::vector<Enemy> enemies;
    const double spacing = EnemyDefinition{}.radius * 2.02;
    for (double z = c.origin.z + 0.5; z < c.origin.z + c.height - 0.5 && enemies.size() < count; z += spacing) {
        for (double x = c.origin.x + 0.5; x < c.origin.x + 9.5 && enemies.size() < count; x += spacing) {
            const auto surface = navigation.project({x, c.origin.y, z}, 0.03);
            if (!surface || !navigation.contains({x, c.origin.y, z}, 0.03)) {
                continue;
            }
            Enemy enemy;
            enemy.despawnOnArrival = true;
            enemy.id = enemies.size() + 1;
            enemy.position = surface->position;
            enemies.push_back(std::move(enemy));
        }
    }
    require(enemies.size() == count, "authored chicken map did not fit agents");
    CrowdMovement movement;
    int frame = 0;
    std::size_t active = count;
    for (; frame < 60 * 240 && active; ++frame) {
        movement.update(navigation,
                        glm::dvec2{c.origin.x + c.width - 0.63, c.origin.z + c.height - 0.71},
                        enemies,
                        1.0 / 60);
        active = 0;
        for (const auto& enemy : enemies) {
            active += enemy.active ? 1 : 0;
        }
        if (frame % 60 == 0) {
            checkClearance(navigation, enemies);
        }
    }
    std::cout << "Authored chicken map: " << frame / 60.0 << " simulated seconds, " << active << " remaining; "
              << navigation.statistics().pathQueries << " searches.\n";
    require(active == 0, "authored map narrow passages caused a persistent deadlock");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected chicken map path");
        checkDirectMovement();
        checkEditsAndInvalidQueries();
        checkRecoveryAndSpikes();
        checkCompactSpacing();
        checkMixedNavigation();
        checkCrowd(100, false);
        checkCrowd(100, true);
        checkCrowd(500, false);
        checkChickenMap(argv[1]);
        std::cout << "PASS continuous NavMesh movement, cached paths, edits, mixed radii, separation, corridors and "
                     "frame rates.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL enemy crowd: " << error.what() << '\n';
        return 1;
    }
}
