#include "navigation/agent.hpp"

#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace grimlock;
using namespace grimlock::game;

namespace {

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

bool near(double a, double b) {
    return std::abs(a - b) < 1e-8;
}

void checkCosts() {
    Grid grid{{5, 4, 2, {-8, 7, -6}}};
    FlowField field;
    require(field.update(grid, GridCell{4, 3}), "initial field was not built");
    require(near(field.cost({0, 0}), 3 * std::numbers::sqrt2 + 1), "open-grid diagonal cost incorrect");
    require(near(field.cost({4, 0}), 3) && field.cost({4, 3}) == 0 && !field.next({4, 3}),
            "cardinal or destination cost incorrect");
    require(!field.next({5, 0}) && !std::isfinite(field.cost({5, 0})), "out-of-bounds field query accepted");

    for (std::size_t index = 0; index < grid.size(); ++index) {
        const auto cell = *grid.cell(index);
        require(grid.worldToCell(*grid.cellCenter(cell)) == cell, "world/cell conversion changed");
        if (const auto next = field.next(cell)) {
            require(canTraverse(grid, cell, *next) && field.cost(*next) < field.cost(cell),
                    "field does not descend through traversable neighbors");
            const auto direction = field.directions()[index];
            require(std::abs(direction.x * direction.x + direction.y * direction.y - 1) < 1e-6,
                    "flow direction is not normalized");
        }
    }
    const auto generation = field.generation();
    const auto* costs = field.costs().data();
    require(!field.update(grid, GridCell{4, 3}) && field.generation() == generation, "unchanged field recalculated");
    grid.setWalkable({1, 1}, false);
    require(field.update(grid, GridCell{4, 3}) && field.generation() == generation + 1 &&
                !std::isfinite(field.cost({1, 1})) && field.costs().data() == costs,
            "grid revision did not refresh field or reuse storage");
    require(field.update(grid, GridCell{0, 0}) && field.cost({0, 0}) == 0, "destination change did not recalculate");
    grid.setWalkable({0, 0}, false);
    field.update(grid, GridCell{0, 0});
    for (auto cost : field.costs()) {
        require(!std::isfinite(cost), "blocked destination retained reachable costs");
    }
    require(field.update(grid, GridCell{99, 0}) && field.update(grid, std::nullopt),
            "invalid destination was not handled");
    grid.configure({3, 2, 0.5, {-10, -2, 3}});
    field.update(grid, GridCell{2, 1});
    require(field.costs().size() == 6 && field.cost({2, 1}) == 0, "resizing retained stale field dimensions");
    Grid replacement{{3, 2, 0.5, {-10, -2, 3}}};
    require(field.update(replacement, GridCell{2, 1}), "equal-revision replacement grid was not rebuilt");
    field.invalidate();
    require(!field.current(replacement) && field.update(replacement, GridCell{2, 1}), "cache invalidation failed");

    Grid maximum{{256, 256, 1, {0, 0, 0}}};
    field.update(maximum, GridCell{255, 255});
    require(near(field.cost({0, 0}), 255 * std::numbers::sqrt2), "maximum grid costs incorrect");
}

void checkCorners() {
    Grid grid{{2, 2, 1, {0, 0, 0}}};
    FlowField field;
    grid.setWalkable({1, 0}, false);
    field.update(grid, GridCell{1, 1});
    require(field.cost({0, 0}) == 2 && field.next({0, 0}) == GridCell{0, 1} && !canTraverse(grid, {0, 0}, {1, 1}),
            "diagonal cut a single blocked corner");
    require(!segmentWalkable(grid, {0.5, 0, 0.5}, {1.5, 0, 1.5}, 0.22), "swept movement cut a blocked corner");
    grid.setWalkable({0, 1}, false);
    field.update(grid, GridCell{1, 1});
    require(!std::isfinite(field.cost({0, 0})) && !field.next({0, 0}), "isolated cell remained reachable");
    require(!canTraverse(grid, {0, 0}, {99, 99}) && !canTraverse(grid, {1, 1}, {1, 1}),
            "non-neighbor traversal accepted");
    require(!segmentWalkable(grid, {0.1, 0, 0.5}, {0.3, 0, 0.5}, 0.22), "agent footprint left the grid");
}

Grid corridor() {
    Grid grid{{7, 7, 1, {-3, 2, -3}}};
    for (std::uint32_t z = 0; z < 7; ++z) {
        if (z != 5) {
            grid.setWalkable({3, z}, false);
        }
    }
    return grid;
}

NavigationAgent makeAgent(const Grid& grid, GridCell start) {
    NavigationAgent agent;
    agent.position = *grid.cellCenter(start);
    agent.position.y = 2.16;
    agent.speed = 2;
    agent.radius = 0.23;
    return agent;
}

void checkMovement() {
    auto grid = corridor();
    FlowField field;
    field.update(grid, GridCell{6, 0});
    const auto generation = field.generation();
    std::array agents{makeAgent(grid, {0, 0}), makeAgent(grid, {0, 3}), makeAgent(grid, {1, 6})};
    agents[1].speed = 1.4;
    agents[2].speed = 2.4;
    for (int frame = 0; frame < 1200; ++frame) {
        require(!field.update(grid, GridCell{6, 0}), "movement caused per-agent field rebuilding");
        for (auto& agent : agents) {
            advanceAgent(grid, field, agent, 1.0 / 60);
            const auto cell = grid.worldToCell(agent.position);
            require(cell && grid.walkable(*cell) &&
                        segmentWalkable(grid, agent.position, agent.position, agent.radius) && agent.position.y == 2.16,
                    "agent entered a wall, left the floor or changed vertical position");
        }
    }
    for (auto& agent : agents) {
        require(agent.status == AgentStatus::arrived && grid.worldToCell(agent.position) == GridCell{6, 0},
                "agent did not reach the destination around the corridor wall");
        const auto position = agent.position;
        advanceAgent(grid, field, agent, 1);
        require(agent.position == position, "arrived agent kept moving");
    }
    require(field.generation() == generation, "agents did not share one field");

    glm::dvec3 reference{};
    for (const int fps : {15, 30, 60, 144}) {
        auto agent = makeAgent(grid, {0, 0});
        for (int frame = 0; frame < fps * 4; ++frame) {
            advanceAgent(grid, field, agent, 1.0 / fps);
        }
        if (fps == 15) {
            reference = agent.position;
        }
        require(glm::length(agent.position - reference) < 1e-8, "movement depends on frame rate");
    }
    auto largeDelta = makeAgent(grid, {0, 0});
    advanceAgent(grid, field, largeDelta, 60);
    require(largeDelta.status == AgentStatus::arrived, "large delta tunneled or failed to stop at destination");

    auto changing = makeAgent(grid, {0, 0});
    advanceAgent(grid, field, changing, 0.2);
    field.update(grid, GridCell{0, 6});
    for (int frame = 0; frame < 600; ++frame) {
        advanceAgent(grid, field, changing, 1.0 / 60);
        require(segmentWalkable(grid, changing.position, changing.position, changing.radius),
                "mid-edge destination change clipped a wall");
    }
    require(changing.status == AgentStatus::arrived && grid.worldToCell(changing.position) == GridCell{0, 6},
            "agent did not follow changed destination");

    changing = makeAgent(grid, {0, 0});
    grid.setWalkable({3, 5}, false);
    field.update(grid, GridCell{6, 0});
    const auto position = changing.position;
    advanceAgent(grid, field, changing, 1);
    require(changing.position == position && changing.status == AgentStatus::unreachable,
            "unreachable agent did not stop");
    grid.setWalkable({3, 5}, true);
    advanceAgent(grid, field, changing, 1);
    require(changing.position == position, "agent used a stale field after grid edits");
    field.update(grid, GridCell{6, 0});
    advanceAgent(grid, field, changing, 0.1);
    require(changing.position != position, "agent failed to resume after wall removal");
    const auto occupied = *grid.worldToCell(changing.position);
    grid.setWalkable(occupied, false);
    field.update(grid, GridCell{6, 0});
    const auto blockedPosition = changing.position;
    advanceAgent(grid, field, changing, 1);
    require(changing.position == blockedPosition && changing.status == AgentStatus::blocked,
            "editing a wall under an agent did not stop it safely");

    Grid open{{8, 3, 1, {0, 0, 0}}};
    field.update(open, GridCell{7, 1});
    auto steady = makeAgent(open, {0, 1});
    const double startX = steady.position.x;
    for (int frame = 0; frame < 10; ++frame) {
        open.setWalkable({0, 0}, frame % 2 != 0);
        field.update(open, GridCell{7, 1});
        advanceAgent(open, field, steady, 0.05);
    }
    require(near(steady.position.x, startX + 1), "unrelated wall edits repeatedly recentered a moving agent");
}

} // namespace

int main() {
    try {
        checkCosts();
        checkCorners();
        checkMovement();
        std::cout << "PASS shared Flow Field costs, corners, cache, corridors, dynamic edits and frame rates.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Flow Field: " << error.what() << '\n';
        return 1;
    }
}
