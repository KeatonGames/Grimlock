#include <cmath>
#include <grimlock/terrain.hpp>
#include <stdexcept>

namespace grimlock {

std::vector<TerrainBox> terrainBoxes(const Grid& grid) {
    const double size = grid.configuration().cellSize;
    const glm::vec3 floorScale{size * 0.5, size * 0.1, size * 0.5};
    const glm::vec3 wallScale{size * 0.5, size * 0.75, size * 0.5};
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(wallScale[axis]) || !std::isfinite(1.0F / floorScale[axis])) {
            throw std::invalid_argument("Grid cell size cannot be represented by the renderer.");
        }
    }

    std::vector<TerrainBox> boxes;
    boxes.reserve(grid.size() * 2);
    const auto append = [&](glm::dvec3 center, glm::vec3 scale, bool floor) {
        const glm::vec3 position{center};
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
            throw std::invalid_argument("Grid origin and extent cannot be represented by the renderer.");
        }
        boxes.push_back({position, scale, floor});
    };
    for (std::size_t index = 0; index < grid.size(); ++index) {
        auto center = *grid.cellCenter(*grid.cell(index));
        center.y -= size * 0.1;
        append(center, floorScale, true);
    }
    for (std::size_t index = 0; index < grid.size(); ++index) {
        const auto cell = *grid.cell(index);
        if (!grid.walkable(cell)) {
            auto center = *grid.cellCenter(cell);
            center.y += size * 0.75;
            append(center, wallScale, false);
        }
    }
    return boxes;
}

} // namespace grimlock
