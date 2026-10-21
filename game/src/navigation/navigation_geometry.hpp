#pragma once

#include <array>
#include <glm/vec3.hpp>
#include <span>
#include <vector>

namespace grimlock {
struct TerrainBox;
}

namespace grimlock::game {

// World-space triangle soup. Blocking triangles rasterize solid geometry but
// never create walkable surfaces, even when their normals face upward.
struct NavigationGeometry {
    std::vector<float> vertices;
    std::vector<int> triangles;
    std::vector<unsigned char> walkable;
    std::vector<std::array<glm::vec3, 2>> solidBoxes;

    void addTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, bool surface);
    void addBox(glm::vec3 minimum, glm::vec3 maximum, bool walkableTop);
};

[[nodiscard]] NavigationGeometry navigationGeometry(std::span<const TerrainBox> boxes);

} // namespace grimlock::game
