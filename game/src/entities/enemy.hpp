#pragma once

#include "entities/enemy_definition.hpp"
#include "navigation/nav_mesh.hpp"

#include <grimlock/ecs.hpp>
#include <grimlock/engine.hpp>
#include <optional>
#include <string>

namespace grimlock::game {

using EnemyId = ecs::Entity; // Generation-checked handle belonging to the owning EnemySystem registry.

enum class DefensiveTargetKind {
    none,
    hero,
    tower,
    objective
};

struct DefensiveTarget {
    DefensiveTargetKind kind = DefensiveTargetKind::none;
    std::uint64_t id = 0;
    bool operator==(const DefensiveTarget&) const = default;
};

struct EnemyCombatState {
    DefensiveTarget target, rejected;
    DefensiveTarget attacker;
    double threatRemaining = 0;
    glm::dvec3 origin{0}, pathTarget{0};
    glm::dvec3 rejectedOrigin{0};
    bool pursuitExhausted = false;
    std::optional<glm::dvec2> objective;
    double objectiveHeight = 0;
    double cooldown = 0, impactRemaining = -1, searchDelay = 0, pathDelay = 0, rejectDelay = 0;
    bool managed = false, swinging = false;
    std::uint64_t pathRevision = 0;
    std::size_t candidateOffset = 0;
};

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

struct EnemyRuntimeComponent {
    EnemyCombatState combat;
    EnemyId id = 0;
    std::uint64_t spawnOrder = 0; // Deterministic gameplay ordering is independent of recycled ECS handle bits.
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
    std::optional<double> destinationHeight;
    double height = 0.5;
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

// Keep existing movement/AI and read-only packed-span consumers on the same authoritative component data.
using Enemy = EnemyRuntimeComponent;

inline std::uint64_t enemyOrder(const Enemy& enemy) noexcept {
    return enemy.spawnOrder ? enemy.spawnOrder : enemy.id; // Standalone CPU fixtures may use unregistered records.
}

struct SpawnArea {
    glm::dvec2 minimum{0}, maximum{1}; // World X/Z rectangle, including its boundary.
    double height = 0;                 // Height hint when projecting onto a navigable surface.
    bool operator==(const SpawnArea&) const = default;
};

} // namespace grimlock::game
