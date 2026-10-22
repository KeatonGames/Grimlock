#pragma once

#include "entities/enemy.hpp"

namespace grimlock::game {

// Decisions run before and after each bounded movement substep. No rendering or instance ownership.
class EnemyAi final {
public:
    static void setDestination(Enemy& enemy, std::optional<glm::dvec2> destination);
    static void prepare(Enemy& enemy, const NavMesh& navigation, double delta, bool despawnOnArrival);
    static void observeMovement(Enemy& enemy, double traveled, double delta);
};

} // namespace grimlock::game
