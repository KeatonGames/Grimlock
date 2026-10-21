#pragma once

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

struct EnemyDefinition {
    std::filesystem::path model = "models/chicken.glb";
    double speed = 1.5;
    double radius = 0.22;
    double acceleration = 8;
    double deceleration = 12;
    float visualHeight = 0.36F; // World-space model height, independent of collision radius.
    float maximumHealth = 100;
    std::string movementAnimation = "walk";
    float animationSpeed = 1;
    double forwardYaw = 0;
    bool despawnOnArrival = true;
};

struct Enemy {
    EnemyId id = 0;
    std::optional<InstanceId> instance;
    glm::dvec3 position{0}; // Foot position on the navigable surface.
    glm::dvec2 velocity{0};
    glm::dvec2 desiredVelocity{0};
    double speed = 1.5, radius = 0.22, yaw = 0;
    double acceleration = 8, deceleration = 12;
    float health = 100;
    MovementStatus movement = MovementStatus::idle;
    bool active = true, alive = true, animationMoving = false;
    NavigationPath path;
    std::optional<glm::dvec2> requestedDestination;
    double repathDelay = 0, stuckTime = 0;
    bool retryPath = false;
};

struct SpawnArea {
    glm::dvec2 minimum{0}, maximum{1}; // World X/Z rectangle, including its boundary.
    double height = 0;                 // Height hint when projecting onto a navigable surface.
};

} // namespace grimlock::game
