#pragma once

#include "grimlock/grid.hpp"

#include <cstddef>
#include <vector>

namespace grimlock::renderer {

// The shared cube spans [-1, 1]; scale stores its half extents.
struct GridCubeInstance {
    glm::vec3 position;
    glm::vec3 scale;
};

static_assert(sizeof(GridCubeInstance) == 24);
static_assert(offsetof(GridCubeInstance, scale) == 12);

struct GridInstances {
    std::vector<GridCubeInstance> cubes; // All floors first, then blocked-cell walls.
    std::uint32_t floorCount = 0;
};

void updateGridInstances(const Grid& grid, GridInstances& instances);

} // namespace grimlock::renderer
