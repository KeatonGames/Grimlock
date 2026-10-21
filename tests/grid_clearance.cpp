#include "navigation/grid_clearance.hpp"

#include <glm/geometric.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace grimlock;
using namespace grimlock::game;

namespace {

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void checkWalls() {
    Grid grid{{8, 8, 1, {0, 0, 0}}};
    grid.setWalkable({3, 3}, false);
    require(segmentWalkable(grid, {2.79, 0, 2.79}, {2.79, 0, 2.79}, 0.25),
            "circular clearance incorrectly used a square footprint");
    require(!segmentWalkable(grid, {2.79, 0, 2.79}, {3.2, 0, 3.2}, 0.25), "sweep cut a blocked corner");
    require(segmentWalkable(grid, {2.75, 0, 1.5}, {2.75, 0, 5.5}, 0.25), "tangent wall movement was blocked");
    const glm::dvec3 start{2.5, 2.7, 3.5};
    const auto slid = slideAlongWalls(grid, start, {1, 0.4}, 0.25);
    require(slid.x <= 2.75 && slid.z > 3.85 && slid.y == start.y && segmentWalkable(grid, start, slid, 0.25),
            "wall collision did not preserve tangential movement or height");
    const auto stopped = slideAlongWalls(grid, start, {20, 0}, 0.25);
    require(stopped.x <= 2.75 && segmentWalkable(grid, start, stopped, 0.25), "long sweep tunneled through a wall");
    require(!segmentWalkable(grid, {3.1, 0, 3.5}, {3.1, 0, 3.5}, 0.25), "circle inside a wall was accepted");
    grid.setWalkable({3, 3}, true);
    require(segmentWalkable(grid, start, {6.75, 2.7, 3.5}, 0.25), "wall removal did not immediately clear movement");
}

void checkWorldCoordinates() {
    Grid grid{{2, 2, 1, {-1, 5, -1}}};
    const glm::dvec3 from{-50.37, 2.3, -30.23}, to{100.47, 2.3, 40.12};
    require(segmentWalkable(grid, from, to, 0.22), "terrain rectangle became an invisible wall");
    const auto moved = slideAlongWalls(grid, from, {to.x - from.x, to.z - from.z}, 0.22);
    require(glm::length(moved - to) < 1e-10, "terrain bounds restricted free world movement");
    require(segmentWalkable(grid, {1e12, 0, -1e12}, {1e12 + 10, 0, -1e12 + 20}, 0.22),
            "distant world coordinates were not supported");
    grid.setWalkable({1, 1}, false);
    require(!segmentWalkable(grid, {-10, 0, 0.5}, {10, 0, 0.5}, 0.22), "outside-to-outside sweep missed a wall");
    require(!segmentWalkable(grid, {10, 0, 0.5}, {-10, 0, 0.5}, 0.22), "reverse sweep missed a wall");
    require(segmentWalkable(grid, {-10, 0, 3.5}, {10, 0, 3.5}, 0.22), "outside motion was blocked by unrelated walls");
    require(!segmentWalkable(grid, from, to, -0.1) &&
                !segmentWalkable(grid, from, {std::numeric_limits<double>::infinity(), 0, 0}, 0.22),
            "invalid collision queries were accepted");
}

} // namespace

int main() {
    try {
        checkWalls();
        checkWorldCoordinates();
        std::cout << "PASS circle clearance, wall sliding, corners, edits and unrestricted world coordinates.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL grid clearance: " << error.what() << '\n';
        return 1;
    }
}
