#include "navigation/grid_clearance.hpp"
#include "systems/crowd_movement.hpp"

#include <chrono>
#include <cmath>
#include <glm/geometric.hpp>
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

std::vector<Enemy> makeEnemies(std::size_t count, double spacing = 0.5) {
    std::vector<Enemy> enemies;
    for (std::size_t index = 0; index < count; ++index) {
        Enemy enemy;
        enemy.id = index + 1;
        enemy.position = {0.5 + static_cast<double>(index % 20) * spacing,
                          0,
                          0.5 + static_cast<double>(index / 20) * spacing};
        enemy.radius = index % 2 ? 0.22 : 0.18;
        enemies.push_back(enemy);
    }
    return enemies;
}

void checkClearance(const Grid* walls, std::span<const Enemy> enemies) {
    for (std::size_t i = 0; i < enemies.size(); ++i) {
        if (!enemies[i].active) {
            continue;
        }
        require(!walls || segmentWalkable(*walls, enemies[i].position, enemies[i].position, enemies[i].radius),
                "enemy entered a wall");
        require(std::isfinite(enemies[i].yaw), "invalid enemy orientation");
        for (std::size_t j = i + 1; j < enemies.size(); ++j) {
            if (enemies[j].active) {
                require(glm::length(enemies[i].position - enemies[j].position) + 1e-8 >=
                            enemies[i].radius + enemies[j].radius,
                        "enemies overlapped");
            }
        }
    }
}

void checkDirectMovement() {
    const glm::dvec2 destination{20.37, 11.83};
    glm::dvec3 reference{0};
    for (const int fps : {15, 30, 60, 144}) {
        auto enemies = makeEnemies(1);
        auto& enemy = enemies[0];
        enemy.position = {1.3, 3.2, -2.1};
        enemy.acceleration = 4;
        const auto start = enemy.position;
        const auto direction = glm::normalize(glm::dvec3{destination.x - start.x, 0, destination.y - start.z});
        CrowdMovement movement;
        for (int frame = 0; frame < fps * 4; ++frame) {
            movement.update(nullptr, destination, enemies, 1.0 / fps);
            const auto offset = enemy.position - start;
            require(glm::length(offset - direction * glm::dot(offset, direction)) < 1e-7 && enemy.position.y == 3.2,
                    "movement left the direct arbitrary-angle route or changed height");
        }
        if (fps == 15) {
            reference = enemy.position;
        }
        require(glm::length(enemy.position - reference) < 0.05, "direct movement depends excessively on frame rate");
        const double speed = glm::length(enemy.velocity);
        enemy.speed = 0;
        movement.update(nullptr, destination, enemies, 1.0 / 60);
        require(glm::length(enemy.velocity) < speed && glm::length(enemy.velocity) > 0, "braking was not smooth");
        for (int frame = 0; frame < 60; ++frame) {
            movement.update(nullptr, destination, enemies, 1.0 / 60);
        }
        require(glm::length(enemy.velocity) < 1e-8, "enemy did not stop after braking");
        enemy.speed = 1.5;
        for (int frame = 0; frame < fps * 20 && enemy.active; ++frame) {
            movement.update(nullptr, destination, enemies, 1.0 / fps);
        }
        require(!enemy.active && enemy.movement == MovementStatus::arrived &&
                    glm::length(glm::dvec2{enemy.position.x, enemy.position.z} - destination) <= enemy.radius + 1e-8,
                "enemy did not reach a non-grid destination");
        const auto stopped = enemy.position;
        movement.update(nullptr, destination, enemies, 0.1);
        require(enemy.position == stopped, "arrived enemy kept moving");
    }
}

void checkGridIndependence() {
    Grid a{{2, 3, 0.25, {100, 8, -50}}}, b{{64, 32, 3, {-20, -5, -20}}};
    auto first = makeEnemies(12, 0.7), second = first, withoutGrid = first;
    CrowdMovement one, two, three;
    for (int frame = 0; frame < 240; ++frame) {
        one.update(&a, glm::dvec2{23.71, 14.19}, first, 1.0 / 60);
        two.update(&b, glm::dvec2{23.71, 14.19}, second, 1.0 / 60);
        three.update(nullptr, glm::dvec2{23.71, 14.19}, withoutGrid, 1.0 / 60);
        for (std::size_t i = 0; i < first.size(); ++i) {
            require(glm::length(first[i].position - second[i].position) < 1e-7 &&
                        glm::length(first[i].position - withoutGrid[i].position) < 1e-7,
                    "empty grid origin, dimensions or cell size changed movement");
        }
    }
    checkClearance(nullptr, first);
    const auto position = first[0].position;
    one.update(&a, std::nullopt, first, 0.1);
    require(first[0].position == position && first[0].movement == MovementStatus::idle,
            "clearing the destination did not stop enemies");
}

void checkWallsAndEdits() {
    Grid grid{{12, 8, 1, {0, 0, 0}}};
    for (std::uint32_t z = 0; z < 8; ++z) {
        grid.setWalkable({5, z}, false);
    }
    auto enemies = makeEnemies(1);
    enemies[0].position = {2, 0, 3.5};
    const glm::dvec2 destination{10.37, 3.5};
    CrowdMovement movement;
    for (int frame = 0; frame < 600; ++frame) {
        movement.update(&grid, destination, enemies, 1.0 / 60);
        checkClearance(&grid, enemies);
    }
    require(enemies[0].position.x < 4.83 && std::abs(enemies[0].position.z - 3.5) < 1e-8 &&
                glm::length(enemies[0].velocity) < 1e-3,
            "enemy crossed the wall or invented an obstacle route");
    const auto stopped = enemies[0].position;
    grid.setWalkable({5, 3}, true);
    movement.update(&grid, destination, enemies, 0.25);
    require(enemies[0].position.x > stopped.x, "wall removal did not immediately resume direct movement");
    const auto occupied = *grid.worldToCell(enemies[0].position);
    grid.setWalkable(occupied, false);
    const auto blocked = enemies[0].position;
    movement.update(&grid, destination, enemies, 0.1);
    require(enemies[0].position == blocked && enemies[0].movement == MovementStatus::blocked,
            "wall painted under an enemy did not stop it");
    grid.setWalkable(occupied, true);
    for (int frame = 0; frame < 600 && enemies[0].active; ++frame) {
        movement.update(&grid, destination, enemies, 1.0 / 60);
        checkClearance(&grid, enemies);
    }
    require(enemies[0].movement == MovementStatus::arrived, "enemy did not pass through the cleared wall");

    auto large = makeEnemies(1);
    large[0].position = {2, 0, 3.5};
    large[0].radius = 0.55;
    for (int frame = 0; frame < 600; ++frame) {
        movement.update(&grid, destination, large, 1.0 / 60);
        checkClearance(&grid, large);
    }
    require(large[0].position.x < 5 && glm::length(large[0].velocity) < 0.01,
            "large footprint crossed a narrow passage");
}

void checkCrowd(std::size_t count) {
    auto enemies = makeEnemies(count);
    CrowdMovement movement;
    const auto start = std::chrono::steady_clock::now();
    std::size_t remaining = count;
    int frame = 0;
    for (; frame < 1800 && remaining; ++frame) {
        movement.update(nullptr, glm::dvec2{20.37, 17.29}, enemies, 0.1);
        if (frame % 10 == 0) {
            checkClearance(nullptr, enemies);
        }
        remaining = 0;
        for (const auto& enemy : enemies) {
            remaining += enemy.active ? 1 : 0;
        }
    }
    const auto milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << count << " enemies: " << frame * 0.1 << "s simulated, " << milliseconds << " ms CPU, " << remaining
              << " remaining.\n";
    require(remaining == 0, "direct crowd did not arrive at its shared world destination");
}

void checkRecoveryAndSpikes() {
    auto enemies = makeEnemies(2);
    enemies[0].position = {0.201, 0, 5.5};
    enemies[1].position = {0.401, 0, 5.5};
    enemies[0].speed = enemies[1].speed = 0;
    Grid grid{{12, 8, 1, {-1, 0, 0}}};
    grid.setWalkable({0, 5}, false);
    CrowdMovement movement;
    for (int frame = 0; frame < 12; ++frame) {
        movement.update(&grid, glm::dvec2{8, 4}, enemies, 1.0 / 60);
    }
    checkClearance(&grid, enemies);
    enemies[0].position = enemies[1].position = {3, 0, 3};
    for (int frame = 0; frame < 12; ++frame) {
        movement.update(nullptr, std::nullopt, enemies, 1.0 / 60);
    }
    checkClearance(nullptr, enemies);

    auto spike = makeEnemies(1);
    const auto before = spike[0].position;
    movement.update(nullptr, glm::dvec2{20.37, 17.29}, spike, 30);
    require(movement.metrics().substeps <= 64 &&
                glm::length(spike[0].position - before) <= spike[0].speed * 0.25 + 1e-8,
            "frame spike caused excessive work or unsafe movement");
    auto sparse = makeEnemies(500, 0.8);
    movement.update(nullptr, glm::dvec2{60.37, 60.29}, sparse, 1.0 / 60);
    require(movement.metrics().neighborChecks < sparse.size() * 30, "neighbor search approached all-pairs work");
    checkClearance(nullptr, sparse);
}

} // namespace

int main() {
    try {
        checkDirectMovement();
        checkGridIndependence();
        checkWallsAndEdits();
        checkCrowd(100);
        checkCrowd(500);
        checkRecoveryAndSpikes();
        std::cout << "PASS direct world movement, arrival, frame rates, walls, predefined radii and separation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL enemy crowd: " << error.what() << '\n';
        return 1;
    }
}
