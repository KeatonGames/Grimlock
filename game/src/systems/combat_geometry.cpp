#include "systems/combat_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace grimlock::game {
namespace {
bool box(glm::dvec3 start, glm::dvec3 displacement, glm::dvec3 minimum, glm::dvec3 maximum, double& closest) {
    double near = 0, far = closest;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(displacement[axis]) < 1e-12) {
            if (start[axis] < minimum[axis] || start[axis] > maximum[axis]) {
                return false;
            }
            continue;
        }
        const double a = (minimum[axis] - start[axis]) / displacement[axis];
        const double b = (maximum[axis] - start[axis]) / displacement[axis];
        near = std::max(near, std::min(a, b));
        far = std::min(far, std::max(a, b));
        if (near > far) {
            return false;
        }
    }
    closest = near;
    return true;
}

void sphere(glm::dvec3 offset, glm::dvec3 movement, double radius, double& closest) {
    const double c = glm::dot(offset, offset) - radius * radius;
    if (c <= 0) {
        closest = 0;
        return;
    }
    const double a = glm::dot(movement, movement), b = glm::dot(offset, movement);
    const double discriminant = b * b - a * c;
    if (a <= 1e-20 || b >= 0 || discriminant < 0) {
        return;
    }
    const double t = (-b - std::sqrt(discriminant)) / a;
    if (t >= 0 && t < closest) {
        closest = t;
    }
}

bool inside(glm::dvec3 point, const std::array<glm::dvec3, 3>& triangle, glm::dvec3 normal) {
    for (std::size_t i = 0; i < 3; ++i) {
        if (glm::dot(glm::cross(triangle[(i + 1) % 3] - triangle[i], point - triangle[i]), normal) < -1e-9) {
            return false;
        }
    }
    return true;
}

void triangleHit(glm::dvec3 start,
                 glm::dvec3 movement,
                 double radius,
                 const std::array<glm::dvec3, 3>& triangle,
                 double& closest) {
    auto normal = glm::cross(triangle[1] - triangle[0], triangle[2] - triangle[0]);
    const double length = glm::length(normal);
    if (length < 1e-12) {
        return;
    }
    normal /= length;
    const double distance = glm::dot(start - triangle[0], normal), speed = glm::dot(movement, normal);
    if (std::abs(distance) <= radius && inside(start - normal * distance, triangle, normal)) {
        closest = 0;
    }
    if (std::abs(speed) > 1e-12) {
        for (const double side : {-1.0, 1.0}) {
            const double t = (side * radius - distance) / speed;
            if (t >= 0 && t < closest && inside(start + movement * t - normal * (side * radius), triangle, normal)) {
                closest = t;
            }
        }
    }
    if (radius == 0) {
        return;
    }
    // Swept sphere versus each edge capsule, including its endpoint spheres.
    for (std::size_t i = 0; i < 3; ++i) {
        const auto a = triangle[i], edge = triangle[(i + 1) % 3] - a, offset = start - a;
        sphere(offset, movement, radius, closest);
        const double squared = glm::dot(edge, edge);
        if (squared < 1e-20) {
            continue;
        }
        const auto perpendicular = offset - edge * (glm::dot(offset, edge) / squared);
        const auto velocity = movement - edge * (glm::dot(movement, edge) / squared);
        double t = closest;
        sphere(perpendicular, velocity, radius, t);
        const double along = glm::dot(offset + movement * t, edge) / squared;
        if (t < closest && along >= 0 && along <= 1) {
            closest = t;
        }
    }
}
} // namespace

void CombatGeometry::synchronize(Engine& engine, std::span<const Tower> towers) {
    source_.synchronize(engine, true);
    if (sourceRevision_ != source_.revision()) {
        build(source_.navigation());
        sourceRevision_ = source_.revision();
    }
    if (towers_.size() != towers.size() || !std::equal(towers_.begin(), towers_.end(), towers.begin())) {
        towers_.assign(towers.begin(), towers.end());
        ++revision_;
    }
}

void CombatGeometry::build(const NavigationGeometry& geometry) {
    if (geometry.triangles.size() % 3 || geometry.vertices.size() % 3) {
        throw std::invalid_argument("Combat geometry requires complete triangles and positions.");
    }
    triangles_.clear();
    nodes_.clear();
    for (std::size_t i = 0; i + 2 < geometry.triangles.size(); i += 3) {
        Triangle triangle;
        triangle.minimum = glm::dvec3{std::numeric_limits<double>::max()};
        triangle.maximum = -triangle.minimum;
        for (std::size_t j = 0; j < 3; ++j) {
            const auto index = static_cast<std::size_t>(geometry.triangles.at(i + j)) * 3;
            triangle.vertices[j] = {geometry.vertices.at(index),
                                    geometry.vertices.at(index + 1),
                                    geometry.vertices.at(index + 2)};
            if (!std::isfinite(glm::length(triangle.vertices[j]))) {
                throw std::invalid_argument("Combat geometry must be finite.");
            }
            triangle.minimum = glm::min(triangle.minimum, triangle.vertices[j]);
            triangle.maximum = glm::max(triangle.maximum, triangle.vertices[j]);
        }
        triangles_.push_back(triangle);
    }
    if (!triangles_.empty()) {
        partition(0, triangles_.size());
    }
    ++revision_;
}

std::size_t CombatGeometry::partition(std::size_t first, std::size_t count) {
    const auto index = nodes_.size();
    Node node;
    node.minimum = glm::dvec3{std::numeric_limits<double>::max()};
    node.maximum = -node.minimum;
    for (std::size_t i = first; i < first + count; ++i) {
        node.minimum = glm::min(node.minimum, triangles_[i].minimum);
        node.maximum = glm::max(node.maximum, triangles_[i].maximum);
    }
    node.first = first;
    node.count = count;
    nodes_.push_back(node);
    if (count > 8) {
        const auto extent = node.maximum - node.minimum;
        const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
        const auto middle = first + count / 2;
        std::nth_element(triangles_.begin() + first,
                         triangles_.begin() + middle,
                         triangles_.begin() + first + count,
                         [axis](const Triangle& a, const Triangle& b) {
                             return a.minimum[axis] + a.maximum[axis] < b.minimum[axis] + b.maximum[axis];
                         });
        const auto left = partition(first, count / 2), right = partition(middle, count - count / 2);
        nodes_[index].count = 0;
        nodes_[index].left = left;
        nodes_[index].right = right;
    }
    return index;
}

void CombatGeometry::visit(std::size_t index,
                           glm::dvec3 start,
                           glm::dvec3 displacement,
                           double radius,
                           double& closest) const {
    const auto& node = nodes_[index];
    double near = closest;
    if (!box(start, displacement, node.minimum - radius, node.maximum + radius, near)) {
        return;
    }
    if (node.count) {
        for (std::size_t i = node.first; i < node.first + node.count; ++i) {
            triangleHit(start, displacement, radius, triangles_[i].vertices, closest);
        }
    } else {
        visit(node.left, start, displacement, radius, closest);
        visit(node.right, start, displacement, radius, closest);
    }
}

std::optional<CombatHit> CombatGeometry::trace(glm::dvec3 start,
                                               glm::dvec3 end,
                                               double radius,
                                               ObjectId ignore,
                                               ObjectId target) const {
    if (!std::isfinite(glm::length(start)) || !std::isfinite(glm::length(end)) || !std::isfinite(radius) ||
        radius < 0) {
        throw std::invalid_argument("Combat traces require finite endpoints and a nonnegative radius.");
    }
    const auto displacement = end - start;
    if (glm::length(displacement) < 1e-10) {
        return {};
    }
    double closest = 1;
    ObjectId object = 0;
    if (!nodes_.empty()) {
        visit(0, start, displacement, radius, closest);
    }
    for (const auto& tower : towers_) {
        if ((ignore && tower.object == ignore) || (target && tower.object == target)) {
            continue;
        }
        const auto inverse = glm::inverse(tower.rotation);
        double t = closest;
        if (box(inverse * (start - tower.position),
                inverse * displacement,
                -tower.halfExtent - radius,
                tower.halfExtent + radius,
                t) &&
            t < closest) {
            closest = t;
            object = tower.object;
        }
    }
    return closest < 1 ? std::optional{CombatHit{start + displacement * closest, closest, object}} : std::nullopt;
}
} // namespace grimlock::game
