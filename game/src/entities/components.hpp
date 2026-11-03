#pragma once

#include <limits>

namespace grimlock::game {

enum class Team {
    enemy,
    player
};

struct TeamComponent {
    Team team = Team::enemy;
};

struct ProjectileLifetimeComponent {
    double elapsed = 0;
    double maximum = std::numeric_limits<double>::infinity();
};

} // namespace grimlock::game
