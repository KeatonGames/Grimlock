#include "navigation/agent.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <numbers>
#include <stdexcept>

namespace grimlock::game {

namespace {

bool intersectsCell(glm::dvec2 from, glm::dvec2 to, glm::dvec2 minimum, glm::dvec2 maximum) noexcept {
    const auto direction = to - from;
    double enter = 0, leave = 1;

    for (int axis = 0; axis < 2; ++axis) {
        if (direction[axis] == 0) {
            if (from[axis] < minimum[axis] || from[axis] > maximum[axis]) {
                return false;
            }
            continue;
        }

        const double a = (minimum[axis] - from[axis]) / direction[axis];
        const double b = (maximum[axis] - from[axis]) / direction[axis];
        enter = std::max(enter, std::min(a, b));
        leave = std::min(leave, std::max(a, b));
        if (enter > leave) {
            return false;
        }
    }

    return true;
}
} // namespace

bool segmentWalkable(const Grid& grid, glm::dvec3 from, glm::dvec3 to, double radius) noexcept {
    const auto& c = grid.configuration();
    const glm::dvec2 a{(from.x - c.origin.x) / c.cellSize, (from.z - c.origin.z) / c.cellSize};
    const glm::dvec2 b{(to.x - c.origin.x) / c.cellSize, (to.z - c.origin.z) / c.cellSize};
    const double margin = radius / c.cellSize;
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y) ||
        !std::isfinite(margin) || margin < 0 || std::min(a.x, b.x) < margin || std::min(a.y, b.y) < margin ||
        std::max(a.x, b.x) > c.width - margin || std::max(a.y, b.y) > c.height - margin) {
        return false;
    }

    const int minX = std::max(0, static_cast<int>(std::floor(std::min(a.x, b.x) - margin)));
    const int minZ = std::max(0, static_cast<int>(std::floor(std::min(a.y, b.y) - margin)));
    const int maxX = std::min(static_cast<int>(c.width) - 1, static_cast<int>(std::floor(std::max(a.x, b.x) + margin)));
    const int maxZ =
        std::min(static_cast<int>(c.height) - 1, static_cast<int>(std::floor(std::max(a.y, b.y) + margin)));

    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            if (!grid.walkable({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z)}) &&
                intersectsCell(a, b, {x - margin, z - margin}, {x + 1 + margin, z + 1 + margin})) {
                return false;
            }
        }
    }

    return true;
}

void advanceAgent(const Grid& grid, const FlowField& field, NavigationAgent& agent, double delta) {
    if (!std::isfinite(delta) || delta < 0 || !std::isfinite(agent.speed) || agent.speed < 0 ||
        !std::isfinite(agent.radius) || agent.radius < 0 || !std::isfinite(agent.yaw) ||
        !std::isfinite(agent.speed * delta)) {
        throw std::invalid_argument("Navigation needs finite nonnegative delta, speed and radius, and finite yaw.");
    }

    const auto cell = grid.worldToCell(agent.position);
    agent.status = AgentStatus::unreachable;
    if (!cell || !segmentWalkable(grid, agent.position, agent.position, agent.radius)) {
        agent.status = AgentStatus::blocked;
        agent.waypoint.reset();
        return;
    }

    if (!field.current(grid) || !std::isfinite(field.cost(*cell))) {
        agent.waypoint.reset();
        return;
    }

    if (agent.fieldGeneration != field.generation()) {
        // Keep a safe downhill edge when editing elsewhere, avoiding repeated recentering/jitter.
        bool keepWaypoint = false;
        if (agent.waypoint && field.cost(*agent.waypoint) <= field.cost(*cell)) {
            if (auto target = grid.cellCenter(*agent.waypoint)) {
                keepWaypoint = segmentWalkable(grid, agent.position, *target, agent.radius);
            }
        }
        if (!keepWaypoint) {
            agent.waypoint = cell;
        }
        agent.fieldGeneration = field.generation();
    }

    double remaining = agent.speed * delta;
    std::optional<double> facing;
    agent.status = AgentStatus::idle;

    // Every completed edge strictly lowers the shared integration cost.
    for (std::size_t step = 0; step <= grid.size(); ++step) {
        const auto current = *grid.worldToCell(agent.position);

        if (!agent.waypoint) {
            agent.waypoint = field.next(current);
            if (!agent.waypoint) {
                agent.status = field.destination() == current ? AgentStatus::arrived : AgentStatus::unreachable;
                break;
            }
        }

        auto target = *grid.cellCenter(*agent.waypoint);
        target.y = agent.position.y;
        const auto offset = target - agent.position;
        const double distance = glm::length(offset);
        if (distance <= grid.configuration().cellSize * 1e-9) {
            agent.position = target;
            agent.waypoint.reset();
            continue;
        }

        if (remaining <= 0) {
            break;
        }

        const double travel = std::min(distance, remaining);
        const auto position = travel == distance ? target : agent.position + offset * (travel / distance);
        if (!segmentWalkable(grid, agent.position, position, agent.radius)) {
            agent.status = AgentStatus::blocked;
            agent.waypoint.reset();
            break;
        }

        agent.position = position;
        remaining -= travel;
        facing = std::atan2(offset.x, offset.z);
        agent.status = AgentStatus::moving;
        if (travel < distance) {
            break;
        }
        agent.waypoint.reset();
    }

    if (facing) {
        // Smooth orientation only. Translation follows safe center-to-center segments.
        const double angle = std::remainder(*facing - agent.yaw, 2 * std::numbers::pi);
        agent.yaw += angle * -std::expm1(-12 * delta);
    }
}

} // namespace grimlock::game
