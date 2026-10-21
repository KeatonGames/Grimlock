#pragma once

#include <grimlock/grid.hpp>
#include <vector>

namespace grimlock {

struct TerrainBox {
    glm::vec3 center{0};
    glm::vec3 halfExtent{0};
    bool walkableTop = false;
};

// Shared world-space geometry for rendering and other consumers. Floors precede walls.
[[nodiscard]] std::vector<TerrainBox> terrainBoxes(const Grid& grid);

} // namespace grimlock
