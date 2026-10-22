#include "systems/enemy_ai.hpp"

#include "systems/crowd_movement.hpp"

#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace grimlock::game;

namespace {

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

NavigationGeometry floor(bool divided = false) {
    NavigationGeometry geometry;
    geometry.addBox({0, -0.2F, 0}, {12, 0, 8}, true);
    if (divided) {
        geometry.addBox({5, 0, 0}, {6, 2, 8}, false);
    }
    return geometry;
}

Enemy animal(glm::dvec3 position, EnemyId id) {
    Enemy enemy;
    enemy.id = id;
    enemy.position = position;
    enemy.despawnOnArrival = false;
    return enemy;
}

void checkStatesAndIndependentGoals() {
    NavMesh navigation;
    require(navigation.build(floor()), "floor build failed");
    std::array enemies{animal({1, 0, 1}, 1), animal({10, 0, 6}, 2)};
    CrowdMovement movement;
    movement.update(navigation, enemies, 0.1);
    require(enemies[0].state == EnemyState::idle && enemies[0].position.x == 1, "Idle moved");
    EnemyAi::setDestination(enemies[0], glm::dvec2{10, 1});
    EnemyAi::setDestination(enemies[1], glm::dvec2{1, 6});
    enemies[0].stoppingDistance = 0.6;
    movement.update(navigation, enemies, 0.1);
    require(enemies[0].state == EnemyState::moving && enemies[1].state == EnemyState::moving &&
                enemies[0].velocity.x > 0 && enemies[1].velocity.x < 0,
            "independent destinations did not start movement");
    for (int frame = 0; frame < 900; ++frame) {
        movement.update(navigation, enemies, 1.0 / 60);
    }
    require(enemies[0].state == EnemyState::reached && enemies[1].state == EnemyState::reached && enemies[0].active &&
                enemies[1].active,
            "Reached animals were not retained");
    require(enemies[0].remainingDistance <= 0.6 && enemies[0].remainingDistance > 0.4 &&
                glm::length(enemies[0].velocity) == 0,
            "custom stopping distance ignored");
    require(navigation.statistics().pathQueries == 2, "terminal states recalculated paths");
    const auto before = enemies[0].position;
    EnemyAi::setDestination(enemies[0], std::nullopt);
    movement.update(navigation, enemies, 0.1);
    require(enemies[0].state == EnemyState::idle && enemies[0].position == before, "clearing goal did not stop");
    EnemyAi::setDestination(enemies[0], glm::dvec2{1, 1});
    movement.update(navigation, enemies, 0.1);
    require(enemies[0].state == EnemyState::moving && enemies[0].velocity.x < 0, "Reached could not retarget");
    bool rejected = false;
    try {
        EnemyAi::setDestination(enemies[0], glm::dvec2{std::numeric_limits<double>::infinity(), 0});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && enemies[0].destination == glm::dvec2{1, 1}, "invalid goal changed state");
}

void checkUnreachableAndRebuild() {
    NavMesh navigation;
    require(navigation.build(floor(true)), "divided floor build failed");
    std::array enemies{animal({1, 0, 2}, 1)};
    EnemyAi::setDestination(enemies[0], glm::dvec2{10, 2});
    CrowdMovement movement;
    for (int frame = 0; frame < 180; ++frame) {
        movement.update(navigation, enemies, 1.0 / 60);
    }
    require(enemies[0].state == EnemyState::blocked && enemies[0].position.x == 1,
            "unreachable goal did not block safely");
    require(navigation.statistics().pathQueries >= 2 && navigation.statistics().pathQueries <= 4,
            "blocked retry frequency is not bounded");
    require(navigation.build(floor()), "wall removal failed");
    movement.update(navigation, enemies, 0.1);
    require(enemies[0].state == EnemyState::moving && enemies[0].position.x > 1,
            "mesh revision did not immediately recover a blocked enemy");
    navigation.reset();
    const auto before = enemies[0].position;
    movement.update(navigation, enemies, 0.1);
    require(enemies[0].state == EnemyState::blocked && enemies[0].position == before,
            "missing navigation did not stop movement");
}

void checkStuckRecovery() {
    NavigationGeometry geometry;
    geometry.addBox({0, -0.2F, 0}, {12, 0, 0.8F}, true);
    NavMesh navigation;
    NavMeshConfiguration configuration;
    configuration.cellSize = 0.05F;
    configuration.agentRadius = 0.195F;
    require(navigation.build(geometry, configuration), navigation.status().c_str());
    std::array enemies{animal({1, 0, 0.4}, 1), animal({3, 0, 0.4}, 2)};
    enemies[0].stuckTimeout = 0.4;
    enemies[0].retryInterval = 0.5;
    EnemyAi::setDestination(enemies[0], glm::dvec2{10, 0.4});
    CrowdMovement movement;
    bool blocked = false;
    for (int frame = 0; frame < 300; ++frame) {
        movement.update(navigation, enemies, 1.0 / 60);
        blocked = blocked || enemies[0].state == EnemyState::blocked;
        require(glm::length(enemies[0].position - enemies[1].position) + 2e-5 >= enemies[0].radius + enemies[1].radius,
                "stuck recovery caused overlap");
    }
    require(blocked && enemies[0].position.x < 3 && enemies[1].state == EnemyState::idle,
            "physical obstruction was not detected as Blocked");
    require(navigation.statistics().pathQueries < 12, "stuck enemy searched every frame");
    enemies[1].active = false;
    for (int frame = 0; frame < 600; ++frame) {
        movement.update(navigation, enemies, 1.0 / 60);
    }
    require(enemies[0].state == EnemyState::reached, "blocked enemy did not recover after obstruction removal");
}

} // namespace

int main() {
    try {
        checkStatesAndIndependentGoals();
        checkUnreachableAndRebuild();
        checkStuckRecovery();
        std::cout << "PASS enemy FSM, independent goals, stopping distance, bounded retries and stuck recovery.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL enemy AI: " << error.what() << '\n';
        return 1;
    }
}
