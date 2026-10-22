#pragma once

#include "entities/enemy.hpp"

namespace grimlock::game {

struct Projectile {
    EnemyId target = 0; // Stable ID, never a pointer into packed enemy storage.
    glm::dvec3 position{0};
    double speed = 0, radius = 0;
    float damage = 0;
    InstanceId instance;
};

} // namespace grimlock::game
