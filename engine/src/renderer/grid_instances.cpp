#include "renderer/grid_instances.hpp"

#include <cmath>
#include <stdexcept>

namespace grimlock::renderer {

void updateGridInstances(const Grid& grid, GridInstances& instances) {
    const auto size = grid.configuration().cellSize;
    const glm::vec3 floorScale{size * 0.5, size * 0.1, size * 0.5};
    const glm::vec3 wallScale{size * 0.5, size * 0.75, size * 0.5};

    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(wallScale[axis]) || !std::isfinite(1.0F / floorScale[axis])) {
            throw std::invalid_argument("Grid cell size cannot be represented by the renderer.");
        }
    }

    instances.cubes.clear();
    instances.floorCount = static_cast<std::uint32_t>(grid.size());
    instances.cubes.reserve(grid.size() * 2);

    for (std::size_t index = 0; index < grid.size(); ++index) {
        auto center = *grid.cellCenter(*grid.cell(index));
        center.y -= size * 0.1; // Keep the floor surface on the logical grid plane.
        const glm::vec3 position{center};

        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
            throw std::invalid_argument("Grid origin and extent cannot be represented by the renderer.");
        }

        instances.cubes.push_back({position, floorScale});
    }

    for (std::size_t index = 0; index < grid.size(); ++index) {
        const auto cell = *grid.cell(index);

        if (!grid.walkable(cell)) {
            auto center = *grid.cellCenter(cell);
            center.y += size * 0.75;
            const glm::vec3 position{center};

            if (!std::isfinite(position.y)) {
                throw std::invalid_argument("Grid wall height cannot be represented by the renderer.");
            }

            instances.cubes.push_back({position, wallScale});
        }
    }
}

} // namespace grimlock::renderer
