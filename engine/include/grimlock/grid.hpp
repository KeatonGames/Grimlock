#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/vec3.hpp>
#include <optional>
#include <vector>

namespace grimlock {
struct GridCell {
    std::uint32_t x = 0, z = 0;
    bool operator==(const GridCell&) const = default;
};

struct GridConfiguration {
    std::uint32_t width = 16, height = 16;
    double cellSize = 1;
    glm::dvec3 origin{0}; // Minimum X/Z corner; Y is the logical/debug plane, not terrain geometry.
    bool operator==(const GridConfiguration&) const = default;
};

struct GridNeighbors {
    std::array<GridCell, 4> cells{};
    std::size_t count = 0;
};

// Generic, CPU-only rectangular data. No collision, diagonal policy or pathfinding.
class Grid final {
public:
    static constexpr std::size_t maximumCells =
        65536; // One byte per cell, bounded debug/scene data.
    explicit Grid(GridConfiguration configuration = {});
    void configure(
        GridConfiguration configuration); // Preserve blocked states in overlapping coordinates.

    [[nodiscard]] const GridConfiguration& configuration() const noexcept { return configuration_; }

    [[nodiscard]] std::size_t size() const noexcept { return walkable_.size(); }

    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    [[nodiscard]] std::optional<std::size_t> index(GridCell cell) const noexcept;
    [[nodiscard]] std::optional<GridCell> cell(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<GridCell> worldToCell(glm::dvec3 world) const noexcept;
    [[nodiscard]] std::optional<glm::dvec3> cellCenter(GridCell cell) const noexcept;
    [[nodiscard]] GridNeighbors neighbors4(GridCell cell) const noexcept;
    [[nodiscard]] bool walkable(GridCell cell) const noexcept; // Out of bounds is non-walkable.
    bool setWalkable(GridCell cell,
                     bool value) noexcept; // True only when an in-bounds cell changed.
private:
    GridConfiguration configuration_{};
    std::vector<std::uint8_t> walkable_;
    std::uint64_t revision_ = 0;
};
} // namespace grimlock
