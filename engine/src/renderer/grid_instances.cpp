#include "renderer/grid_instances.hpp"

#include <grimlock/terrain.hpp>

namespace grimlock::renderer {

void updateGridInstances(const Grid& grid, GridInstances& instances) {
    const auto boxes = terrainBoxes(grid);
    instances.cubes.clear();
    instances.floorCount = static_cast<std::uint32_t>(grid.size());
    instances.cubes.reserve(boxes.size());
    for (const auto& box : boxes) {
        instances.cubes.push_back({box.center, box.halfExtent});
    }
}

} // namespace grimlock::renderer
