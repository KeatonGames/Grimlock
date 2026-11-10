#pragma once

#include "entities/components.hpp"
#include "entities/enemy.hpp"

namespace grimlock::game {

struct ProjectileDefinition {
    float damage = 1;
    double speed = 12, radius = 0.06;
    glm::vec4 tint{1, 0.85F, 0.35F, 1};
    double splashRadius = 0;
    std::filesystem::path model;
    float visualScale = 0;
    DamageType damageType = DamageType::physical;
};

struct ProjectileComponent {
    EnemyId target = 0; // Stable ID, never a pointer into packed enemy storage.
    glm::dvec3 position{0};
    double speed = 0, radius = 0;
    float damage = 0;
    InstanceId instance;
    ObjectId source = 0;
    glm::dvec3 velocity{0};
    ecs::Entity sourceEntity = 0;
    DefensiveTargetKind sourceKind = DefensiveTargetKind::tower;
    double splashRadius = 0;
    float visualScale = 0;
    DamageType damageType = DamageType::physical;
    bool whiteArrowTrail = false;
};

using Projectile = ProjectileComponent;

} // namespace grimlock::game
