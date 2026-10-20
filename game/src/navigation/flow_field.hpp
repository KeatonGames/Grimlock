#pragma once

#include <glm/vec2.hpp>
#include <grimlock/grid.hpp>
#include <optional>
#include <span>
#include <vector>

namespace grimlock::game {

[[nodiscard]] bool canTraverse(const Grid& grid, GridCell from, GridCell to) noexcept;

class FlowField final {
public:
    // Costs are measured in cells: cardinal steps cost 1, diagonals sqrt(2).
    // The grid must outlive queries; invalidate the cache before releasing it.
    bool update(const Grid& grid, std::optional<GridCell> destination);
    void invalidate() noexcept;
    [[nodiscard]] double cost(GridCell cell) const noexcept;
    // Neighbor at the end of the shared direction vector; agents target its center.
    [[nodiscard]] std::optional<GridCell> next(GridCell cell) const noexcept;

    [[nodiscard]] std::span<const double> costs() const noexcept {
        return costs_;
    }

    [[nodiscard]] std::span<const glm::vec2> directions() const noexcept {
        return directions_;
    }

    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }

    [[nodiscard]] std::optional<GridCell> destination() const noexcept {
        return destination_;
    }

    [[nodiscard]] bool current(const Grid& grid) const noexcept {
        return grid_ == &grid && revision_ == grid.revision();
    }

private:
    struct Entry {
        double cost;
        std::uint32_t index;
    };

    const Grid* grid_ = nullptr;
    std::uint64_t revision_ = 0, generation_ = 0;
    std::optional<GridCell> destination_;
    std::vector<double> costs_;
    std::vector<glm::vec2> directions_;
    std::vector<std::uint32_t> next_;
    std::vector<Entry> frontier_;
};

} // namespace grimlock::game
