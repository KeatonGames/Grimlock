#include "navigation/navigation_geometry.hpp"

#include <algorithm>
#include <glm/geometric.hpp>
#include <grimlock/terrain.hpp>
#include <limits>

namespace grimlock::game {

void NavigationGeometry::addTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, bool surface) {
    const int first = static_cast<int>(vertices.size() / 3);
    for (const auto p : {a, b, c}) {
        vertices.insert(vertices.end(), {p.x, p.y, p.z});
    }
    triangles.insert(triangles.end(), {first, first + 1, first + 2});
    walkable.push_back(surface ? 1 : 0);
}

void NavigationGeometry::addBox(glm::vec3 minimum, glm::vec3 maximum, bool walkableTop) {
    if (!walkableTop) {
        solidBoxes.push_back({minimum, maximum});
    }
    const glm::vec3 a{minimum.x, minimum.y, minimum.z}, b{maximum.x, minimum.y, minimum.z},
        c{maximum.x, minimum.y, maximum.z}, d{minimum.x, minimum.y, maximum.z}, e{minimum.x, maximum.y, minimum.z},
        f{maximum.x, maximum.y, minimum.z}, g{maximum.x, maximum.y, maximum.z}, h{minimum.x, maximum.y, maximum.z};
    addTriangle(e, h, g, walkableTop);
    addTriangle(e, g, f, walkableTop);
    addTriangle(a, b, c, false);
    addTriangle(a, c, d, false);
    addTriangle(a, e, f, false);
    addTriangle(a, f, b, false);
    addTriangle(b, f, g, false);
    addTriangle(b, g, c, false);
    addTriangle(c, g, h, false);
    addTriangle(c, h, d, false);
    addTriangle(d, h, e, false);
    addTriangle(d, e, a, false);
}

void NavigationGeometry::addSolidVolume(std::span<const glm::vec3> points) {
    if (points.empty()) {
        return;
    }
    SolidVolume volume;
    volume.minimumHeight = std::numeric_limits<float>::max();
    volume.maximumHeight = -volume.minimumHeight;
    std::vector<glm::vec3> sorted{points.begin(), points.end()};
    for (const auto point : points) {
        volume.minimumHeight = std::min(volume.minimumHeight, point.y);
        volume.maximumHeight = std::max(volume.maximumHeight, point.y);
    }
    std::sort(sorted.begin(), sorted.end(), [](auto a, auto b) {
        return a.x < b.x || (a.x == b.x && a.z < b.z);
    });
    sorted.erase(std::unique(sorted.begin(),
                             sorted.end(),
                             [](auto a, auto b) {
                                 return a.x == b.x && a.z == b.z;
                             }),
                 sorted.end());
    if (sorted.size() < 3) {
        return;
    }
    const auto turn = [](auto a, auto b, auto c) {
        return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
    };
    auto& hull = volume.footprint;
    for (const auto point : sorted) {
        while (hull.size() >= 2 && turn(hull[hull.size() - 2], hull.back(), point) <= 0) {
            hull.pop_back();
        }
        hull.push_back(point);
    }
    const auto lower = hull.size();
    for (auto it = sorted.rbegin() + 1; it != sorted.rend(); ++it) {
        while (hull.size() > lower && turn(hull[hull.size() - 2], hull.back(), *it) <= 0) {
            hull.pop_back();
        }
        hull.push_back(*it);
    }
    hull.pop_back();
    if (hull.size() >= 3) {
        solidVolumes.push_back(std::move(volume));
    }
}

void NavigationGeometry::addBox(glm::vec3 minimum, glm::vec3 maximum, const glm::mat4& transform) {
    NavigationGeometry local;
    local.addBox(minimum, maximum, false);
    std::vector<glm::vec3> points;
    points.reserve(local.vertices.size() / 3);
    for (std::size_t i = 0; i < local.vertices.size(); i += 3) {
        points.push_back(transform * glm::vec4{local.vertices[i], local.vertices[i + 1], local.vertices[i + 2], 1});
    }
    for (std::size_t i = 0; i < points.size(); i += 3) {
        addTriangle(points[i], points[i + 1], points[i + 2], false);
    }
    addSolidVolume(points);
}

NavigationGeometry navigationGeometry(std::span<const TerrainBox> boxes) {
    NavigationGeometry result;
    result.vertices.reserve(boxes.size() * 108);
    result.triangles.reserve(boxes.size() * 36);
    result.walkable.reserve(boxes.size() * 12);
    for (std::size_t index = 0; index < boxes.size(); ++index) {
        const auto& box = boxes[index];
        auto minimum = box.center - box.halfExtent;
        auto maximum = box.center + box.halfExtent;
        // Grid floor cubes form coplanar rows. Remove internal faces before
        // rasterization, while preserving actual gaps, elevations and solid walls.
        while (box.walkableTop && index + 1 < boxes.size()) {
            const auto& next = boxes[index + 1];
            const auto low = next.center - next.halfExtent;
            const auto high = next.center + next.halfExtent;
            if (!next.walkableTop || low.y != minimum.y || high.y != maximum.y || low.z != minimum.z ||
                high.z != maximum.z || low.x > maximum.x || high.x < minimum.x) {
                break;
            }
            minimum.x = std::min(minimum.x, low.x);
            maximum.x = std::max(maximum.x, high.x);
            ++index;
        }
        result.addBox(minimum, maximum, box.walkableTop);
    }
    return result;
}

} // namespace grimlock::game
