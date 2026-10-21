#include "navigation/grid_clearance.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace grimlock::game {
namespace {

struct Hit {
    double fraction = 1;
    glm::dvec2 normal{0};
    bool valid = true;
};

void consider(Hit& hit, double fraction, glm::dvec2 normal, glm::dvec2 displacement) {
    if (fraction >= -1e-12 && fraction <= hit.fraction && glm::dot(normal, displacement) < 0) {
        hit.fraction = std::max(0.0, fraction);
        hit.normal = normal;
    }
}

void collideCell(Hit& hit, glm::dvec2 start, glm::dvec2 displacement, double radius, glm::dvec2 minimum) {
    const auto maximum = minimum + 1.0;
    const auto nearest = glm::clamp(start, minimum, maximum);
    const auto offset = start - nearest;
    if (glm::dot(offset, offset) < radius * radius - 1e-12 ||
        (start.x > minimum.x && start.x < maximum.x && start.y > minimum.y && start.y < maximum.y)) {
        hit.valid = false;
        hit.fraction = 0;
        return;
    }

    // Flat faces and circular corners form the exact Minkowski sum of a cell and a circle.
    for (int axis = 0; axis < 2; ++axis) {
        if (displacement[axis] == 0) {
            continue;
        }
        const int other = 1 - axis;
        for (const double sign : {-1.0, 1.0}) {
            const double plane = sign < 0 ? minimum[axis] - radius : maximum[axis] + radius;
            const double t = (plane - start[axis]) / displacement[axis];
            const double along = start[other] + displacement[other] * t;
            if (along >= minimum[other] && along <= maximum[other]) {
                glm::dvec2 normal{0};
                normal[axis] = sign;
                consider(hit, t, normal, displacement);
            }
        }
    }
    const double a = glm::dot(displacement, displacement);
    if (a == 0 || radius <= 0) {
        return;
    }
    for (int z = 0; z < 2; ++z) {
        for (int x = 0; x < 2; ++x) {
            const auto corner = minimum + glm::dvec2{x, z};
            const auto relative = start - corner;
            const double b = glm::dot(relative, displacement);
            const double discriminant = b * b - a * (glm::dot(relative, relative) - radius * radius);
            if (discriminant > 0) {
                const double t = (-b - std::sqrt(discriminant)) / a;
                const auto normal = (relative + displacement * t) / radius;
                consider(hit, t, normal, displacement);
            }
        }
    }
}

template <class Visitor>
void visitWalls(const Grid& grid, glm::dvec2 from, glm::dvec2 to, double radius, Visitor visit) {
    const auto& c = grid.configuration();
    const auto minimum = glm::min(from, to) - radius;
    const auto maximum = glm::max(from, to) + radius;
    if (maximum.x < 0 || maximum.y < 0 || minimum.x > c.width || minimum.y > c.height) {
        return;
    }
    const int minX = static_cast<int>(std::clamp(std::floor(minimum.x), 0.0, double(c.width - 1)));
    const int minZ = static_cast<int>(std::clamp(std::floor(minimum.y), 0.0, double(c.height - 1)));
    const int maxX = static_cast<int>(std::clamp(std::floor(maximum.x), 0.0, double(c.width - 1)));
    const int maxZ = static_cast<int>(std::clamp(std::floor(maximum.y), 0.0, double(c.height - 1)));
    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            if (!grid.walkable({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z)}) &&
                !visit(glm::dvec2{x, z})) {
                return;
            }
        }
    }
}

Hit sweep(const Grid& grid, glm::dvec3 position, glm::dvec2 displacement, double radius, bool stopAtFirst = false) {
    const auto& c = grid.configuration();
    const glm::dvec2 from{(position.x - c.origin.x) / c.cellSize, (position.z - c.origin.z) / c.cellSize};
    const auto delta = displacement / c.cellSize;
    radius /= c.cellSize;
    const auto to = from + delta;
    Hit hit;
    if (!std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(to.x) || !std::isfinite(to.y) ||
        !std::isfinite(radius) || radius < 0) {
        return {0, {0, 0}, false};
    }
    visitWalls(grid, from, from + delta * hit.fraction, radius, [&](glm::dvec2 cell) {
        collideCell(hit, from, delta, radius, cell);
        return !stopAtFirst || (hit.valid && hit.fraction >= 1);
    });
    return hit;
}

} // namespace

bool segmentWalkable(const Grid& grid, glm::dvec3 from, glm::dvec3 to, double radius) noexcept {
    if (!std::isfinite(from.y) || !std::isfinite(to.y)) {
        return false;
    }
    const auto hit = sweep(grid, from, {to.x - from.x, to.z - from.z}, radius, true);
    return hit.valid && hit.fraction >= 1 - 1e-12;
}

glm::dvec3 slideAlongWalls(const Grid& grid, glm::dvec3 position, glm::dvec2 displacement, double radius) noexcept {
    const auto start = position;
    const double skin = grid.configuration().cellSize * 1e-7;
    for (int contact = 0; contact < 3 && glm::dot(displacement, displacement) > 1e-22; ++contact) {
        const auto hit = sweep(grid, position, displacement, radius);
        if (!hit.valid) {
            return start;
        }
        const double fraction = std::max(0.0, hit.fraction - skin / glm::length(displacement));
        position += glm::dvec3{displacement.x, 0, displacement.y} * (hit.fraction == 1 ? 1 : fraction);
        if (hit.fraction == 1) {
            break;
        }
        displacement *= 1 - fraction;
        displacement -= hit.normal * std::min(0.0, glm::dot(displacement, hit.normal));
    }
    if (!segmentWalkable(grid, start, position, radius)) {
        const glm::dvec2 delta{position.x - start.x, position.z - start.z};
        const auto hit = sweep(grid, start, delta, radius);
        return start + glm::dvec3{delta.x, 0, delta.y} *
                           std::max(0.0, hit.fraction - skin / std::max(glm::length(delta), skin));
    }
    return position;
}

} // namespace grimlock::game
