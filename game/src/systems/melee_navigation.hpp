#pragma once

#include "entities/enemy.hpp"
#include "entities/tower.hpp"
#include "navigation/nav_mesh.hpp"

namespace grimlock::game {
class CombatGeometry;

struct MeleeBody {
    glm::dvec3 position{0}; // Foot position.
    double radius = 0;
    glm::dvec2 halfExtent{0}; // Nonzero for oriented rectangular tower footprints.
    double yaw = 0;
};

[[nodiscard]] MeleeBody meleeBody(const Tower& tower);
[[nodiscard]] double meleeGap(glm::dvec3 position, double radius, const MeleeBody& target);
[[nodiscard]] bool meleeContact(const NavMesh& navigation,
                                glm::dvec3 position,
                                double radius,
                                const MeleeBody& target,
                                double reach);
// Samples existing Detour paths to reachable attack positions; never paths to a solid target's center.
[[nodiscard]] bool approachMelee(const NavMesh& navigation,
                                 glm::dvec3 position,
                                 double radius,
                                 const MeleeBody& target,
                                 double reach,
                                 NavigationPath& path,
                                 std::uint64_t slot = 0,
                                 std::span<const Enemy> blockers = {},
                                 EnemyId targetId = 0,
                                 const CombatGeometry* geometry = nullptr,
                                 double height = 0.5,
                                 ObjectId targetObject = 0);

} // namespace grimlock::game
