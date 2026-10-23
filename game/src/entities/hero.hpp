#pragma once

#include "entities/enemy.hpp"
#include "navigation/nav_mesh.hpp"

#include <grimlock/engine.hpp>

namespace grimlock::game {

struct CleaveDefinition {
    float damage = 45;
    double radius = 2, angle = 100, cooldown = 6;
};

struct HeroCombatDefinition {
    float damage = 25;
    double range = 1.2, interval = 0.8, impactTime = 0.25;
    double acquisitionRange = 5, pursuitRange = 8;
    std::string attackAnimation = "attack";
    CleaveDefinition cleave;
};

struct HeroCombatState {
    std::optional<EnemyId> target;
    std::optional<EnemyId> excludedTarget;
    std::optional<EnemyId> unreachableTarget;
    double unreachableRetry = 0;
    std::uint64_t failedRevision = 0;
    glm::dvec3 excludedOrigin{0};
    glm::dvec3 pursuitOrigin{0};
    glm::dvec3 pathTarget{0};
    double pathDelay = 0;
    bool returning = false;
    bool explicitTarget = false, playerMoving = false, swinging = false, abilitySwing = false;
    double cooldown = 0, impactRemaining = 0, cleaveCooldown = 0, feedbackRemaining = 0, retryDelay = 0;
};

using HeroId = std::uint64_t;
enum class HeroState {
    idle,
    moving,
    reached,
    blocked
};

struct HeroDefinition {
    HeroCombatDefinition combat;
    std::string name = "Assassin";
    std::filesystem::path model = "models/assassin.glb";
    double speed = 3.5, radius = 0.28, height = 1.4;
    double acceleration = 12, deceleration = 16, turningSpeed = 12, stoppingDistance = 0.08;
    float visualHeight = 1.25F, maximumHealth = 100;
    double forwardYaw = 0;
    std::string idleAnimation = "idle", walkAnimation = "walk", runAnimation = "run";
};

[[nodiscard]] HeroDefinition loadHeroDefinition(const std::filesystem::path& assets);

struct Hero {
    HeroCombatState combat;
    HeroId id = 0;
    ObjectId object = 0;
    glm::dvec3 position{0};
    glm::dvec3 defensivePosition{0};
    std::optional<EnemyId> avoidanceEnemy;
    glm::dvec3 avoidancePoint{0};
    double hitFlashRemaining = 0;
    double yaw = 0, speed = 0, stuckTime = 0;
    float health = 100, maximumHealth = 100;
    bool controllable = true;
    HeroState state = HeroState::idle;
    std::optional<glm::dvec3> destination;
    NavigationPath path;
    bool animationMoving = false;
};

} // namespace grimlock::game
