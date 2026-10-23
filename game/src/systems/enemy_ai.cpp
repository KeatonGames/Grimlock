#include "systems/enemy_ai.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <stdexcept>

namespace grimlock::game {

namespace {

void stop(Enemy& enemy, EnemyState state, MovementStatus movement) {
    enemy.state = state;
    enemy.movement = movement;
    enemy.velocity = {0, 0};
    enemy.desiredVelocity = {0, 0};
    if (state == EnemyState::idle || state == EnemyState::blocked) {
        enemy.remainingDistance = std::numeric_limits<double>::max();
    }
}

void block(Enemy& enemy) {
    stop(enemy, EnemyState::blocked, MovementStatus::blocked);
    enemy.retryPath = true;
}

} // namespace

const char* enemyStateName(EnemyState state) noexcept {
    switch (state) {
    case EnemyState::idle:
        return "Idle";
    case EnemyState::moving:
        return "Moving";
    case EnemyState::reached:
        return "Reached";
    case EnemyState::blocked:
        return "Blocked";
    }
    return "Unknown";
}

void EnemyAi::setDestination(Enemy& enemy, std::optional<glm::dvec2> destination) {
    if (destination && (!std::isfinite(destination->x) || !std::isfinite(destination->y))) {
        throw std::invalid_argument("Enemy destination must contain finite world X/Z coordinates.");
    }
    enemy.destination = destination;
    enemy.destinationAssigned = true;
    enemy.requestedDestination.reset();
    enemy.path.valid = false;
    enemy.retryPath = true;
    enemy.repathDelay = 0;
    enemy.stuckTime = 0;
    // Retargeting preserves velocity; clearing a destination stops immediately.
    enemy.state = destination ? EnemyState::moving : EnemyState::idle;
    if (!destination) {
        stop(enemy, EnemyState::idle, MovementStatus::idle);
    }
}

void EnemyAi::prepare(Enemy& enemy, const NavMesh& navigation, double delta, bool despawnOnArrival) {
    enemy.desiredVelocity = {0, 0};
    enemy.repathDelay = std::max(0.0, enemy.repathDelay - delta);
    if (!enemy.destination) {
        stop(enemy, EnemyState::idle, MovementStatus::idle);
        return;
    }
    if (!navigation.contains(enemy.position) || enemy.radius > navigation.configuration().agentRadius + 1e-6) {
        block(enemy);
        return;
    }
    const bool changed =
        enemy.requestedDestination != enemy.destination || enemy.path.revision != navigation.revision();
    if (enemy.state == EnemyState::reached && !changed) {
        stop(enemy, EnemyState::reached, MovementStatus::arrived);
        enemy.active = !(despawnOnArrival && enemy.despawnOnArrival && !enemy.combat.managed);
        return;
    }
    if (changed || (enemy.retryPath && enemy.repathDelay <= 0)) {
        const auto goal = *enemy.destination;
        const bool found = navigation.findPath(enemy.position, {goal.x, enemy.position.y, goal.y}, enemy.path);
        enemy.requestedDestination = enemy.destination;
        enemy.retryPath = !found;
        enemy.repathDelay = enemy.retryInterval;
        enemy.stuckTime = 0;
        if (!found) {
            block(enemy);
            return;
        }
    }
    if (enemy.retryPath) {
        block(enemy);
        return;
    }
    const auto target = navigation.steering(enemy.position, enemy.path, &enemy.remainingDistance);
    if (!target) {
        block(enemy);
        return;
    }
    const double stoppingDistance = enemy.combat.managed && enemy.combat.target.kind != DefensiveTargetKind::none
                                        ? 0.03
                                        : (enemy.stoppingDistance > 0 ? enemy.stoppingDistance : enemy.radius);
    if (enemy.remainingDistance <= std::max(1e-4, stoppingDistance)) {
        stop(enemy, EnemyState::reached, MovementStatus::arrived);
        enemy.active = !(despawnOnArrival && enemy.despawnOnArrival && !enemy.combat.managed);
        return;
    }
    const glm::dvec2 offset{target->x - enemy.position.x, target->z - enemy.position.z};
    const double distance = glm::length(offset);
    if (distance < 1e-6) {
        block(enemy);
        return;
    }
    enemy.state = EnemyState::moving;
    enemy.movement = MovementStatus::idle;
    const double speed = std::min(enemy.speed, std::sqrt(2 * enemy.deceleration * enemy.remainingDistance));
    enemy.desiredVelocity = offset * (speed / distance);
}

void EnemyAi::observeMovement(Enemy& enemy, double traveled, double delta) {
    enemy.stuckTime = enemy.speed > 0 && traveled < enemy.speed * delta * 0.02 ? enemy.stuckTime + delta : 0;
    if (enemy.stuckTime >= enemy.stuckTimeout) {
        block(enemy);
        enemy.stuckTime = 0;
        enemy.repathDelay = enemy.retryInterval;
    }
}

} // namespace grimlock::game
