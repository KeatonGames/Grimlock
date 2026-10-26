#pragma once

#include "entities/enemy.hpp"

namespace grimlock::game {

struct TowerAttack {
    ObjectId tower = 0;
    std::optional<EnemyId> target;
    double cooldown = 0;
    double searchDelay = 0;
    std::uint64_t geometryRevision = 0;
    std::size_t enemyCount = 0;
    std::size_t level = 0;
};

} // namespace grimlock::game
