#pragma once

#include "entities/enemy_definition.hpp"
#include "navigation/nav_mesh.hpp"

#include <grimlock/engine.hpp>
#include <optional>
#include <string>

namespace grimlock::game {

using EnemyId = std::uint64_t; // Monotonic within an EnemySystem, independent of packed storage indices.

enum class MovementStatus {
    idle,
    moving,
    arrived,
    blocked
};

enum class EnemyState {
    idle,
    moving,
    reached,
    blocked
};

[[nodiscard]] const char* enemyStateName(EnemyState state) noexcept;

struct Enemy {
    EnemyId id = 0;
    EnemyTypeId type = 0;
    const NavMesh* navigation = nullptr; // Borrowed; the owning session outlives all its enemies.
    std::optional<InstanceId> instance;
    glm::dvec3 position{0}; // Foot position on the navigable surface.
    glm::dvec2 velocity{0};
    glm::dvec2 desiredVelocity{0};
    double speed = 1.5, radius = 0.195, yaw = 0;
    double acceleration = 8, deceleration = 12;
    float maximumHealth = 100, health = 100;
    double hitFlashRemaining = 0;
    MovementStatus movement = MovementStatus::idle;
    EnemyState state = EnemyState::idle;
    std::optional<glm::dvec2> destination;
    double stoppingDistance = 0; // Zero uses the physical radius.
    double retryInterval = 1, stuckTimeout = 2;
    double remainingDistance = 0;
    bool active = true, alive = true, animationMoving = false;
    bool despawnOnArrival = true;
    NavigationPath path;
    std::optional<glm::dvec2> requestedDestination;
    double repathDelay = 0, stuckTime = 0;
    bool retryPath = false;
    bool destinationAssigned = false;
};

struct SpawnArea {
    glm::dvec2 minimum{0}, maximum{1}; // World X/Z rectangle, including its boundary.
    double height = 0;                 // Height hint when projecting onto a navigable surface.
    bool operator==(const SpawnArea&) const = default;
};

} // namespace grimlock::game
