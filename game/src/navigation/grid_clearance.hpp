#pragma once

#include <glm/vec2.hpp>
#include <grimlock/grid.hpp>

namespace grimlock::game {

// Circle swept across the XZ grid. Tangential contact is allowed; penetration is not.
// Only blocked cells are walls; movement may extend beyond the terrain rectangle.
[[nodiscard]] bool segmentWalkable(const Grid& grid, glm::dvec3 from, glm::dvec3 to, double radius) noexcept;
[[nodiscard]] glm::dvec3 slideAlongWalls(const Grid& grid,
                                         glm::dvec3 position,
                                         glm::dvec2 displacement,
                                         double radius) noexcept;

} // namespace grimlock::game
