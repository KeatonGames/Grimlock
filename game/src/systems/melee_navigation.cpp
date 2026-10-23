#include "systems/melee_navigation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace grimlock::game {
namespace {
glm::dvec2 horizontal(glm::dvec3 value) {
    return {value.x, value.z};
}

glm::dvec3 boundary(glm::dvec3 from, const MeleeBody& target) {
    const auto offset = horizontal(from - target.position);
    if (target.halfExtent.x <= 0) {
        const double length = glm::length(offset);
        const auto direction = length > 1e-8 ? offset / length : glm::dvec2{0, 1};
        return target.position + glm::dvec3{direction.x, 0, direction.y} * target.radius;
    }
    const double c = std::cos(target.yaw), s = std::sin(target.yaw);
    const glm::dvec2 local{c * offset.x - s * offset.y, s * offset.x + c * offset.y};
    const auto point = glm::clamp(local, -target.halfExtent, target.halfExtent);
    return target.position + glm::dvec3{c * point.x + s * point.y, 0, -s * point.x + c * point.y};
}
} // namespace

MeleeBody meleeBody(const Tower& tower) {
    const auto forward = tower.rotation * glm::dvec3{0, 0, 1};
    return {tower.position - glm::dvec3{0, tower.halfExtent.y, 0},
            0,
            {tower.halfExtent.x, tower.halfExtent.z},
            std::atan2(forward.x, forward.z)};
}

double meleeGap(glm::dvec3 position, double radius, const MeleeBody& target) {
    return std::max(0.0, glm::length(horizontal(position - boundary(position, target))) - radius);
}

bool meleeContact(const NavMesh& navigation,
                  glm::dvec3 position,
                  double radius,
                  const MeleeBody& target,
                  double reach) {
    if (std::abs(position.y - target.position.y) > navigation.configuration().stepHeight + 0.2 ||
        meleeGap(position, radius, target) > reach + 1e-6) {
        return false;
    }
    const auto edge = boundary(position, target);
    auto offset = horizontal(position - edge);
    const double length = glm::length(offset);
    offset = length > 1e-8 ? offset / length : glm::dvec2{0, 1};
    // Recast erodes the surface by agent radius; test the stand-off point, not the solid body center.
    const double clearance = radius + navigation.configuration().cellSize * 1.5 + 0.02;
    const auto contact = edge + glm::dvec3{offset.x, 0, offset.y} * clearance;
    const auto point = navigation.project(contact, navigation.configuration().cellSize * 2);
    return point && glm::length(horizontal(point->position - contact)) < navigation.configuration().cellSize * 2 &&
           navigation.visible(position, point->position);
}

bool approachMelee(const NavMesh& navigation,
                   glm::dvec3 position,
                   double radius,
                   const MeleeBody& target,
                   double reach,
                   NavigationPath& path,
                   std::uint64_t slot,
                   std::span<const Enemy> blockers,
                   EnemyId targetId) {
    const double base = std::atan2(position.x - target.position.x, position.z - target.position.z);
    const double perimeter = std::max(target.radius, glm::length(target.halfExtent)) + 1;
    // Stable angular staggering spreads crowds across a target's perimeter without reserving slots.
    const double stagger = slot ? (static_cast<double>(slot % 7) - 3) * 0.17 : 0;
    for (int sample = 0; sample < 16; ++sample) {
        const int side = sample % 2 ? (sample + 1) / 2 : -sample / 2;
        const double angle = base + stagger + side * std::numbers::pi / 8;
        const auto probe = target.position + glm::dvec3{std::sin(angle), 0, std::cos(angle)} * perimeter;
        const auto edge = boundary(probe, target);
        const auto outward = glm::normalize(horizontal(probe - edge));
        const auto desired = edge + glm::dvec3{outward.x, 0, outward.y} * (radius + reach * 0.65);
        const auto point = navigation.project(desired, navigation.configuration().cellSize * 2);
        const bool occupied =
            point && std::any_of(blockers.begin(), blockers.end(), [&](const Enemy& enemy) {
                return enemy.id != targetId && enemy.alive && enemy.active &&
                       glm::length(horizontal(point->position - enemy.position)) < radius + enemy.radius;
            });
        if (point && !occupied && meleeContact(navigation, point->position, radius, target, reach) &&
            navigation.findPath(position, point->position, path)) {
            return true;
        }
    }
    return false;
}

} // namespace grimlock::game
