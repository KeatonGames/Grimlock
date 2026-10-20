#pragma once

#include "navigation/flow_field.hpp"

namespace grimlock::game {

enum class AgentStatus {
    idle,
    moving,
    arrived,
    unreachable,
    blocked
};

struct NavigationAgent {
    glm::dvec3 position{0};
    double speed = 1.5;
    double yaw = 0;
    double radius = 0.25; // Conservative X/Z footprint, in world units.
    AgentStatus status = AgentStatus::idle;
    std::optional<GridCell> waypoint;
    std::uint64_t fieldGeneration = 0;
};

// Swept X/Z clearance against grid cells only; this is not a physics/collision system.
[[nodiscard]] bool segmentWalkable(const Grid& grid, glm::dvec3 from, glm::dvec3 to, double radius) noexcept;
void advanceAgent(const Grid& grid, const FlowField& field, NavigationAgent& agent, double delta);

} // namespace grimlock::game
