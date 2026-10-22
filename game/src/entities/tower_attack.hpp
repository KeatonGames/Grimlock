#pragma once

#include "entities/enemy.hpp"

namespace grimlock::game {

struct TowerAttack {
    ObjectId tower = 0;
    std::optional<EnemyId> target;
    double cooldown = 0;
};

} // namespace grimlock::game
