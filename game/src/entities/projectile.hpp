#pragma once

#include "entities/components.hpp"
#include "entities/enemy.hpp"

namespace grimlock::game {

struct ProjectileComponent {
    EnemyId target = 0; // Stable ID, never a pointer into packed enemy storage.
    glm::dvec3 position{0};
    double speed = 0, radius = 0;
    float damage = 0;
    InstanceId instance;
    ObjectId source = 0;
    glm::dvec3 velocity{0};
    ecs::Entity sourceEntity = 0;
};

using Projectile = ProjectileComponent;

} // namespace grimlock::game
