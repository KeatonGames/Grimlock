#include "navigation/navigation_geometry.hpp"

#include <algorithm>
#include <grimlock/terrain.hpp>

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
