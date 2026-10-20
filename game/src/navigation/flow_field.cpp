#include "navigation/flow_field.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace grimlock::game {

namespace {

constexpr std::uint32_t noCell = std::numeric_limits<std::uint32_t>::max();
constexpr std::array<glm::ivec2, 8> offsets{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};
} // namespace

bool canTraverse(const Grid& grid, GridCell from, GridCell to) noexcept {
    if (!grid.walkable(from) || !grid.walkable(to)) {
        return false;
    }

    const auto dx = std::abs(static_cast<int>(to.x) - static_cast<int>(from.x));
    const auto dz = std::abs(static_cast<int>(to.z) - static_cast<int>(from.z));
    if (dx > 1 || dz > 1 || dx + dz == 0) {
        return false;
    }

    return dx == 0 || dz == 0 || (grid.walkable({from.x, to.z}) && grid.walkable({to.x, from.z}));
}

bool FlowField::update(const Grid& grid, std::optional<GridCell> destination) {
    if (grid_ == &grid && revision_ == grid.revision() && destination_ == destination) {
        return false;
    }

    grid_ = &grid;
    revision_ = grid.revision();
    destination_ = destination;
    ++generation_;
    costs_.assign(grid.size(), std::numeric_limits<double>::infinity());
    directions_.assign(grid.size(), glm::vec2{0});
    next_.assign(grid.size(), noCell);
    frontier_.clear();
    frontier_.reserve(grid.size() * 2);

    if (!destination || !grid.walkable(*destination)) {
        return true;
    }

    const auto compare = [](const Entry& a, const Entry& b) {
        return a.cost > b.cost || (a.cost == b.cost && a.index > b.index);
    };
    const auto goal = static_cast<std::uint32_t>(*grid.index(*destination));
    costs_[goal] = 0;
    frontier_.push_back({0, goal});

    while (!frontier_.empty()) {
        std::pop_heap(frontier_.begin(), frontier_.end(), compare);
        const auto current = frontier_.back();
        frontier_.pop_back();
        if (current.cost != costs_[current.index]) {
            continue;
        }

        const auto cell = *grid.cell(current.index);

        for (const auto offset : offsets) {
            const int x = static_cast<int>(cell.x) + offset.x;
            const int z = static_cast<int>(cell.z) + offset.y;
            if (x < 0 || z < 0) {
                continue;
            }

            const GridCell neighbor{static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z)};
            if (!canTraverse(grid, cell, neighbor)) {
                continue;
            }

            const auto index = static_cast<std::uint32_t>(*grid.index(neighbor));
            const double step = offset.x && offset.y ? std::numbers::sqrt2 : 1;
            const double candidate = current.cost + step;
            if (candidate >= costs_[index]) {
                continue;
            }

            costs_[index] = candidate;
            next_[index] = current.index;
            directions_[index] = glm::vec2{-offset} / static_cast<float>(step);
            frontier_.push_back({candidate, index});
            std::push_heap(frontier_.begin(), frontier_.end(), compare);
        }
    }

    return true;
}

void FlowField::invalidate() noexcept {
    grid_ = nullptr;
}

double FlowField::cost(GridCell cell) const noexcept {
    const auto index = grid_ ? grid_->index(cell) : std::nullopt;
    return index && *index < costs_.size() ? costs_[*index] : std::numeric_limits<double>::infinity();
}

std::optional<GridCell> FlowField::next(GridCell cell) const noexcept {
    const auto index = grid_ ? grid_->index(cell) : std::nullopt;
    return index && *index < next_.size() && next_[*index] != noCell ? grid_->cell(next_[*index]) : std::nullopt;
}

} // namespace grimlock::game
