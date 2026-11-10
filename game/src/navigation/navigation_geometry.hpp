#pragma once

#include <array>
#include <glm/mat4x4.hpp>
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
    struct SolidVolume {
        std::vector<glm::vec3> footprint;
        float minimumHeight = 0, maximumHeight = 0;
    };

    std::vector<float> vertices;
    std::vector<int> triangles;
    std::vector<unsigned char> walkable;
    std::vector<std::array<glm::vec3, 2>> solidBoxes;
    std::vector<SolidVolume> solidVolumes;

    void addTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, bool surface);
    void addBox(glm::vec3 minimum, glm::vec3 maximum, bool walkableTop);
    void addBox(glm::vec3 minimum, glm::vec3 maximum, const glm::mat4& transform);
    void addSolidVolume(std::span<const glm::vec3> points);
};

[[nodiscard]] NavigationGeometry navigationGeometry(std::span<const TerrainBox> boxes);

} // namespace grimlock::game
