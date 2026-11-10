#include "navigation/nav_mesh.hpp"

#include "ui/hero_route_visual.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
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

void checkRouteVisual() {
    auto geometry = floor();
    geometry.addBox({3, 0, -10}, {5, 1.5F, 8}, false);
    NavMesh mesh;
    require(mesh.build(geometry), "visual route fixture failed");
    NavigationPath path;
    require(mesh.findPath({0, 0, 0}, {10, 0, 0}, path), "visual route path failed");
    const auto polygons = path.polygons;
    HeroRouteVisual visual;
    visual.update(mesh, {0, 0, 0}, path);
    const std::vector<glm::dvec3> route{visual.points().begin(), visual.points().end()};
    require(route.size() > 3 && path.polygons == polygons && glm::distance(route.back(), path.destination) < 1e-6,
            "visual trace changed the live corridor or failed to reach the destination around a corner");
    require(std::all_of(route.begin(),
                        route.end(),
                        [&](glm::dvec3 point) {
                            return mesh.contains(point);
                        }),
            "visual route escaped the movement surface");
    const auto dashes = visual.dashes();
    require(!dashes.empty(), "visual route has no dashes");
    double routeLength = 0, inkLength = 0;
    for (std::size_t i = 1; i < route.size(); ++i) {
        routeLength += glm::distance(route[i - 1], route[i]);
    }
    for (const auto& dash : dashes) {
        inkLength += glm::distance(dash.from, dash.to);
    }
    require(inkLength > routeLength * 0.4 && inkLength < routeLength * 0.8,
            "dashes have no visible gaps or lost most of the route");
    auto previousSize = route.size();
    for (std::size_t i = 1; i + 1 < route.size(); ++i) {
        visual.update(mesh, route[i], path);
        require(!visual.points().empty() && visual.points().front() == route[i] &&
                    visual.points().size() <= previousSize && visual.points().back() == route.back(),
                "walking retained travelled points or made the remaining route grow/flip at a turn");
        const auto remainingDashes = visual.dashes();
        require(!remainingDashes.empty() && glm::distance(remainingDashes.front().from, dashes.front().from) < 1e-6,
                "walking slid the dash pattern instead of consuming it");
        previousSize = visual.points().size();
    }
    require(mesh.build(floor()), "visual route rebuild failed");
    visual.update(mesh, {0, 0, 0}, path);
    require(visual.points().empty() && visual.dashes().empty(), "visual route retained invalid polygons after rebuild");
}

void checkCornerTraversal() {
    NavigationGeometry geometry;
    geometry.addBox({-6, -0.2F, -6}, {6, 0, 6}, true);
    geometry.addBox({-1, 0, -1}, {1, 2, 1}, false);
    NavMesh navigation;
    require(navigation.build(geometry), navigation.status().c_str());
    for (const double z : {-2.0, -0.8, 0.0, 0.8, 2.0}) {
        NavigationPath path;
        require(navigation.findPath({-4, 0, z}, {4, 0, -z}, path), "corner route missing");
        require(!navigation.movementRoute({-4, 0, z}, path).empty(), "corner anticipation pinned a route to a wall");
    }
    NavigationPath corridor;
    require(navigation.findPath({-4, 0, -0.8}, {4, 0, -0.8}, corridor), "corridor repair fixture missing");
    const auto displaced = navigation.project({0, 0, 1.5}, 0.03);
    require(displaced && navigation.steering(displaced->position, corridor).has_value() && corridor.valid,
            "valid local avoidance displacement invalidated the cached corridor instead of repairing it");
}

void checkRotatedObstacle() {
    NavigationGeometry geometry;
    geometry.addBox({-6, -0.2F, -6}, {6, 0, 6}, true);
    const auto transform = glm::rotate(glm::mat4{1}, glm::radians(45.0F), glm::vec3{0, 1, 0});
    geometry.addBox({-2, 0, -0.25F}, {2, 2, 0.25F}, transform);
    NavMesh navigation;
    require(navigation.build(geometry), navigation.status().c_str());
    require(!navigation.contains({0, 0, 0}), "rotated obstacle retained a buried walkable interior");
    require(navigation.contains({1.3, 0, 1.3}), "rotated obstacle blocked the empty corner of its world AABB");
    require(!navigation.debugConnections().empty(), "walkable polygon connections missing");
    navigation.reset();
    require(navigation.debugConnections().empty(), "reset retained stale polygon connections");
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
    require(navigationGeometry(boxes).walkable.size() == (grid.configuration().height + 1) * 12,
            "grid floor rows retained redundant interior faces");

    for (const double cellSize : {0.5, 1.0, 1.3}) {
        Grid example{{12, 8, cellSize, {-6, 2, -4}}};
        example.setWalkable({5, 3}, false);
        example.setWalkable({5, 4}, false);
        example.setWalkable({6, 4}, false);
        const auto original = terrainBoxes(example);
        NavigationGeometry unmerged;
        for (const auto& box : original) {
            unmerged.addBox(box.center - box.halfExtent, box.center + box.halfExtent, box.walkableTop);
        }
        NavMesh reference, merged;
        require(reference.build(unmerged) && merged.build(navigationGeometry(original)),
                "floor comparison build failed");
        for (int z = 0; z <= 16; ++z) {
            for (int x = 0; x <= 24; ++x) {
                const glm::dvec3 point{-6 + x * cellSize * 0.5, 2, -4 + z * cellSize * 0.5};
                require(reference.contains(point) == merged.contains(point),
                        "coalesced floor changed wall/border clearance");
            }
        }
        NavigationPath before, after;
        require(
            reference.findPath({-6 + cellSize, 2, -4 + cellSize}, {-6 + cellSize * 10, 2, -4 + cellSize * 6}, before) &&
                merged.findPath({-6 + cellSize, 2, -4 + cellSize}, {-6 + cellSize * 10, 2, -4 + cellSize * 6}, after),
            "coalesced floor changed a valid wall detour");
    }
    const std::vector<TerrainBox> separated{{{0, -0.1F, 0}, {0.5F, 0.1F, 0.5F}, true},
                                            {{2, -0.1F, 0}, {0.5F, 0.1F, 0.5F}, true},
                                            {{3, 0.9F, 0}, {0.5F, 0.1F, 0.5F}, true},
                                            {{4, 0.9F, 0}, {0.5F, 0.1F, 0.5F}, false}};
    require(navigationGeometry(separated).walkable.size() == separated.size() * 12,
            "floor optimization bridged a gap, elevation change or wall");
}

} // namespace

int main() {
    try {
        checkQueries();
        checkObstacles();
        checkRouteVisual();
        checkCornerTraversal();
        checkRotatedObstacle();
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
