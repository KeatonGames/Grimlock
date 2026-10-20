#include "grimlock/grid.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace grimlock {

Grid::Grid(GridConfiguration configuration) {
    configure(configuration);
}

void Grid::configure(GridConfiguration configuration) {
    if (!configuration.width || !configuration.height || configuration.width > maximumCells / configuration.height) {
        throw std::invalid_argument("Grid dimensions must be positive and contain at most 65536 cells.");
    }

    const auto& origin = configuration.origin;
    const double endX = origin.x + configuration.width * configuration.cellSize;
    const double endZ = origin.z + configuration.height * configuration.cellSize;

    if (!std::isfinite(configuration.cellSize) || configuration.cellSize <= 0 || !std::isfinite(origin.x) ||
        !std::isfinite(origin.y) || !std::isfinite(origin.z) || !std::isfinite(endX) || !std::isfinite(endZ) ||
        origin.x + configuration.cellSize <= origin.x || origin.z + configuration.cellSize <= origin.z ||
        endX - configuration.cellSize >= endX || endZ - configuration.cellSize >= endZ) {
        throw std::invalid_argument("Grid requires a finite origin and a positive, representable cell size.");
    }

    if (!walkable_.empty() && configuration == configuration_) {
        return;
    }

    if (!walkable_.empty() && configuration.width == configuration_.width &&
        configuration.height == configuration_.height) {
        configuration_ = configuration;
        ++revision_;

        return;
    }

    std::vector<std::uint8_t> replacement(static_cast<std::size_t>(configuration.width) * configuration.height, 1);

    for (std::uint32_t z = 0; !walkable_.empty() && z < std::min(configuration.height, configuration_.height); ++z) {
        for (std::uint32_t x = 0; x < std::min(configuration.width, configuration_.width); ++x) {
            replacement[static_cast<std::size_t>(z) * configuration.width + x] =
                walkable_[static_cast<std::size_t>(z) * configuration_.width + x];
        }
    }

    configuration_ = configuration;
    walkable_ = std::move(replacement);
    ++revision_;
}

std::optional<std::size_t> Grid::index(GridCell cell) const noexcept {
    if (cell.x >= configuration_.width || cell.z >= configuration_.height) {
        return {};
    }

    return static_cast<std::size_t>(cell.z) * configuration_.width + cell.x;
}

std::optional<GridCell> Grid::cell(std::size_t index) const noexcept {
    if (index >= size()) {
        return {};
    }

    return GridCell{static_cast<std::uint32_t>(index % configuration_.width),
                    static_cast<std::uint32_t>(index / configuration_.width)};
}

std::optional<GridCell> Grid::worldToCell(glm::dvec3 world) const noexcept {
    if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z)) {
        return {};
    }

    const auto& configuration = configuration_;

    // Check world bounds before division: the far edges are excluded even if division rounds down.
    if (world.x < configuration.origin.x || world.z < configuration.origin.z ||
        world.x >= configuration.origin.x + configuration.width * configuration.cellSize ||
        world.z >= configuration.origin.z + configuration.height * configuration.cellSize) {
        return {};
    }

    const double x = std::floor((world.x - configuration.origin.x) / configuration.cellSize);
    const double z = std::floor((world.z - configuration.origin.z) / configuration.cellSize);

    // A representable position just inside a far edge can divide to exactly width/height.
    return GridCell{static_cast<std::uint32_t>(std::min(x, static_cast<double>(configuration.width - 1))),
                    static_cast<std::uint32_t>(std::min(z, static_cast<double>(configuration.height - 1)))};
}

std::optional<glm::dvec3> Grid::cellCenter(GridCell cell) const noexcept {
    if (!index(cell)) {
        return {};
    }

    const auto& configuration = configuration_;

    return configuration.origin +
           glm::dvec3{(cell.x + 0.5) * configuration.cellSize, 0, (cell.z + 0.5) * configuration.cellSize};
}

GridNeighbors Grid::neighbors4(GridCell cell) const noexcept {
    GridNeighbors result;

    if (!index(cell)) {
        return result;
    }

    if (cell.x > 0) {
        result.cells[result.count++] = {cell.x - 1, cell.z};
    }

    if (cell.x + 1 < configuration_.width) {
        result.cells[result.count++] = {cell.x + 1, cell.z};
    }

    if (cell.z > 0) {
        result.cells[result.count++] = {cell.x, cell.z - 1};
    }

    if (cell.z + 1 < configuration_.height) {
        result.cells[result.count++] = {cell.x, cell.z + 1};
    }

    return result;
}

bool Grid::walkable(GridCell cell) const noexcept {
    const auto cellIndex = index(cell);

    return cellIndex && walkable_[*cellIndex] != 0;
}

bool Grid::setWalkable(GridCell cell, bool value) noexcept {
    const auto cellIndex = index(cell);

    if (!cellIndex || walkable_[*cellIndex] == static_cast<std::uint8_t>(value)) {
        return false;
    }

    walkable_[*cellIndex] = static_cast<std::uint8_t>(value);
    ++revision_;

    return true;
}

} // namespace grimlock
