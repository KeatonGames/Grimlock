#pragma once

#include <limits>

namespace grimlock::game {

struct HealthComponent {
    float health = 100, maximumHealth = 100;
    double hitFlashRemaining = 0;
};

enum class Team {
    enemy,
    player
};

struct TeamComponent {
    Team team = Team::enemy;
};

inline bool hostile(Team first, Team second) noexcept {
    return first != second;
}

inline bool hostile(const TeamComponent* first, const TeamComponent* second) noexcept {
    return first && second && hostile(first->team, second->team);
}

struct ProjectileLifetimeComponent {
    double elapsed = 0;
    double maximum = std::numeric_limits<double>::infinity();
};

} // namespace grimlock::game
