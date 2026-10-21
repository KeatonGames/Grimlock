#include "navigation/navigation_geometry.hpp"

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
    for (const auto& box : boxes) {
        result.addBox(box.center - box.halfExtent, box.center + box.halfExtent, box.walkableTop);
    }
    return result;
}

} // namespace grimlock::game
