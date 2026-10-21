#include "navigation/nav_mesh.hpp"

#include <cmath>
#include <grimlock/terrain.hpp>
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

NavigationGeometry floor() {
    NavigationGeometry geometry;
    geometry.addBox({-10, -0.2F, -10}, {20, 0, 20}, true);
    return geometry;
}

void checkQueries() {
    NavMesh navigation;
    NavigationPath path;
    require(!navigation.project({0, 0, 0}) && !navigation.findPath({0, 0, 0}, {1, 0, 1}, path),
            "empty mesh accepted a navigation query");
    auto geometry = floor();
    require(navigation.build(geometry), navigation.status().c_str());
    const auto projected = navigation.project({1.37, 0.2, -2.81});
    require(projected && std::abs(projected->position.x - 1.37) < 1e-5 && std::abs(projected->position.z + 2.81) < 1e-5,
            "projection snapped coordinates to cells");
    require(navigation.findPath({-7, 0, -6}, {16.37, 0, 13.81}, path), "open world path failed");
    require(navigation.corners({-7, 0, -6}, path).size() == 2 && navigation.visible({-7, 0, -6}, path.destination),
            "open route added unnecessary corners");
    const auto edge = navigation.move({-7, 0, -6}, {-100, -100});
    require(navigation.contains(edge) && edge.x > -10 && edge.z > -10, "movement escaped navigable terrain");
    require(!navigation.project({1000, 0, 1000}) &&
                !navigation.project({std::numeric_limits<double>::quiet_NaN(), 0, 0}) &&
                !navigation.findPath({0, 0, 0}, {1000, 0, 1000}, path),
            "invalid destination accepted");
    require(navigation.move({0, 0, 0}, {1e300, 0}) == glm::dvec3{0} &&
                navigation.move({0, 0, 0}, {std::numeric_limits<double>::infinity(), 0}) == glm::dvec3{0},
            "invalid movement displacement reached Detour");
    require(navigation.findPath({0, 0, 0}, {10, 0, 10}, path), "cached route setup failed");
    const auto revision = navigation.revision();
    require(navigation.build(geometry) && navigation.revision() != revision && !navigation.steering({0, 0, 0}, path),
            "rebuild did not invalidate old polygon references");
    geometry.triangles[0] = -1;
    require(!navigation.build(geometry) && !navigation.ready() && !navigation.steering({0, 0, 0}, path),
            "failed build retained stale navigation data");
    require(navigation.build(floor()), "recovery after failed build failed");
    auto bad = navigation.configuration();
    bad.agentRadius = -1;
    require(!navigation.build(floor(), bad), "invalid configuration accepted");
    bad.agentRadius = 12.9F;
    require(!navigation.build(floor(), bad), "erosion radius overflow accepted");
    auto invalidBox = floor();
    invalidBox.solidBoxes.push_back({glm::vec3{-1e30F}, glm::vec3{1e30F}});
    require(!navigation.build(invalidBox), "unrepresentable solid box accepted");
}

void checkObstacles() {
    auto geometry = floor();
    geometry.addBox({3, 0, -10}, {5, 1.5F, 8}, false);
    NavMesh navigation;
    require(navigation.build(geometry), navigation.status().c_str());
    NavigationPath path;
    require(navigation.findPath({0, 0, 0}, {10, 0, 0}, path), "wall detour not found");
    const auto corners = navigation.corners({0, 0, 0}, path);
    require(corners.size() >= 4 && !navigation.visible({0, 0, 0}, {10, 0, 0}), "path cut through the wall");
    for (std::size_t i = 1; i < corners.size(); ++i) {
        require(navigation.visible(corners[i - 1], corners[i]), "funnel segment escaped NavMesh");
    }
    require(!navigation.project({4, 1.5, 0}, 0.1), "blocking wall top became walkable");
    const auto stopped = navigation.move({2, 0, 0}, {4, 0});
    require(stopped.x <= 3 - navigation.configuration().agentRadius, "wall contact ignored physical radius");

    geometry = floor();
    geometry.addBox({3, 0, -10}, {5, 1.5F, 20}, false);
    require(navigation.build(geometry) && !navigation.findPath({0, 0, 0}, {10, 0, 0}, path),
            "disconnected route was treated as complete");
    require(navigation.build(floor()) && navigation.findPath({0, 0, 0}, {10, 0, 0}, path),
            "wall removal did not restore navigation");
}

void checkConfiguration() {
    NavigationGeometry corridor;
    corridor.addBox({0, -0.2F, 0}, {10, 0, 5}, true);
    corridor.addBox({4, 0, 0}, {5, 2, 2.1F}, false);
    corridor.addBox({4, 0, 2.9F}, {5, 2, 5}, false);
    NavMesh small, large;
    NavMeshConfiguration config;
    config.cellSize = 0.05F;
    config.agentRadius = 0.15F;
    require(small.build(corridor, config), small.status().c_str());
    config.agentRadius = 0.5F;
    require(large.build(corridor, config), large.status().c_str());
    NavigationPath path;
    require(small.findPath({2, 0, 2.5}, {8, 0, 2.5}, path) && !large.findPath({2, 0, 2.5}, {8, 0, 2.5}, path),
            "radius did not control narrow-passage clearance");

    NavigationGeometry lowCeiling;
    lowCeiling.addBox({0, -0.2F, 0}, {8, 0, 8}, true);
    lowCeiling.addBox({0, 0.35F, 0}, {8, 1, 8}, false);
    require(!small.build(lowCeiling), "agent height ignored a low ceiling");

    NavigationGeometry steps;
    steps.addBox({0, -0.2F, 0}, {4, 0, 5}, true);
    steps.addBox({4, -0.2F, 0}, {8, 0.1F, 5}, true);
    require(small.build(steps) && small.findPath({2, 0, 2}, {6, 0.1, 2}, path), "valid step was not traversable");
    steps = {};
    steps.addBox({0, -0.2F, 0}, {4, 0, 5}, true);
    steps.addBox({4, -0.2F, 0}, {8, 1, 5}, true);
    require(small.build(steps) && !small.findPath({2, 0, 2}, {6, 1, 2}, path), "maximum climb was ignored");

    NavigationGeometry steep;
    steep.addTriangle({0, 0, 0}, {0, 0, 5}, {3, 6, 5}, true);
    steep.addTriangle({0, 0, 0}, {3, 6, 5}, {3, 6, 0}, true);
    require(!small.build(steep), "steep surface became walkable");
}

void checkTerrainAdapter() {
    Grid grid{{12, 8, 1, {-6, 2, -4}}};
    grid.setWalkable({5, 3}, false);
    const auto boxes = terrainBoxes(grid);
    require(boxes.size() == grid.size() + 1 && boxes.front().walkableTop && !boxes.back().walkableTop,
            "terrain geometry lost floor or wall semantics");
    NavMesh navigation;
    require(navigation.build(navigationGeometry(boxes)), navigation.status().c_str());
    require(!navigation.contains({-0.5, 2, -0.5}) && navigation.contains({-3.5, 2, -0.5}),
            "rendered wall geometry did not match navigation");
}

} // namespace

int main() {
    try {
        checkQueries();
        checkObstacles();
        checkConfiguration();
        checkTerrainAdapter();
        std::cout << "PASS NavMesh geometry, projection, funnel paths, clearance, radius/height/slope/climb, invalid "
                     "queries and rebuilds.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL NavMesh: " << error.what() << '\n';
        return 1;
    }
}
