#pragma once

#include "entities/enemy.hpp"
#include "navigation/nav_mesh.hpp"

#include <grimlock/engine.hpp>

namespace grimlock::game {

enum class HeroAttack {
    melee,
    ranged,
    area,
    rangedArea
};

struct CleaveDefinition {
    float damage = 45;
    double radius = 2, angle = 100, cooldown = 6;
};

struct HeroCombatDefinition {
    HeroAttack attack = HeroAttack::melee;
    double projectileSpeed = 12, projectileRadius = 0.06;
    float damage = 25;
    double range = 1.2, interval = 0.8, impactTime = 0.25;
    double acquisitionRange = 5, pursuitRange = 8;
    std::string attackAnimation = "attack";
    std::string releaseAnimation, aimingAnimation, projectileSocket;
    std::filesystem::path projectileModel;
    glm::vec3 projectileOffset{0};
    glm::vec4 projectileTint{1, 0.85F, 0.35F, 1};
    float projectileScale = 0;
    double splashRadius = 0;
    double aimingTime = 0;
    CleaveDefinition cleave;
    bool specialAbilities = true;
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
    double drawRemaining = -1;
};

using HeroId = ecs::Entity;
enum class HeroState {
    idle,
    moving,
    reached,
    blocked
};

struct HeroDefinition {
    std::string id = "assassin"; // Preserve the legacy type key for authored levels.
    HeroCombatDefinition combat;
    std::string name = "Knight";
    std::filesystem::path model = "heroes/knight/knight.glb";
    double speed = 3.5, radius = 0.28, height = 1.4;
    double acceleration = 12, deceleration = 16, turningSpeed = 12, stoppingDistance = 0.08;
    float visualHeight = 1.25F, maximumHealth = 100;
    double forwardYaw = 0;
    std::string hitAnimation, deathAnimation = "death";
    std::string idleAnimation = "idle", walkAnimation = "walk", runAnimation = "run";
};

[[nodiscard]] HeroDefinition loadHeroDefinition(const std::filesystem::path& assets);
[[nodiscard]] std::vector<HeroDefinition> loadHeroDefinitions(const std::filesystem::path& assets);

struct HeroRuntimeComponent : HealthComponent {
    std::size_t type = 0;
    HeroCombatState combat;
    HeroId id = 0;
    std::uint64_t spawnOrder = 0;
    ObjectId object = 0;
    glm::dvec3 position{0};
    glm::dvec3 defensivePosition{0};
    std::optional<EnemyId> avoidanceEnemy;
    glm::dvec3 avoidancePoint{0};
    double yaw = 0, speed = 0, stuckTime = 0;
    double visualYaw = 0;
    bool controllable = true;
    HeroState state = HeroState::idle;
    std::optional<glm::dvec3> destination;
    NavigationPath path;
    bool animationMoving = false;
    double hitAnimationRemaining = 0;
    std::optional<std::size_t> locomotionClip;
};

using Hero = HeroRuntimeComponent;

} // namespace grimlock::game
