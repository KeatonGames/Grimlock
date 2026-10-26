#include "navigation/level_geometry.hpp"

#include "entities/tower.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace grimlock::game {
namespace {
using Polygon = std::vector<glm::dvec2>;

double signedArea(std::span<const glm::dvec2> polygon) {
    double area = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto& a = polygon[i];
        const auto& b = polygon[(i + 1) % polygon.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5;
}

Polygon clipEdge(const Polygon& polygon, glm::dvec2 origin, glm::dvec2 edge, double sign) {
    Polygon result;
    if (polygon.empty()) {
        return result;
    }
    const auto distance = [&](glm::dvec2 point) {
        const auto offset = point - origin;
        return sign * (edge.x * offset.y - edge.y * offset.x);
    };
    auto previous = polygon.back();
    double before = distance(previous);
    for (const auto current : polygon) {
        const double after = distance(current);
        if ((before >= 0) != (after >= 0)) {
            result.push_back(previous + (current - previous) * (before / (before - after)));
        }
        if (after >= 0) {
            result.push_back(current);
        }
        previous = current;
        before = after;
    }
    return result;
}

void subtractSupport(std::vector<Polygon>& unsupported, std::span<const glm::dvec3> surface) {
    Polygon cover;
    for (const auto vertex : surface) {
        cover.push_back({vertex.x, vertex.z});
    }
    const double sign = signedArea(cover) >= 0 ? 1 : -1;
    std::vector<Polygon> remaining;
    for (auto piece : unsupported) {
        for (std::size_t i = 0; i < cover.size() && !piece.empty(); ++i) {
            const auto edge = cover[(i + 1) % cover.size()] - cover[i];
            auto outside = clipEdge(piece, cover[i], edge, -sign);
            if (std::abs(signedArea(outside)) > 1e-10) {
                remaining.push_back(std::move(outside));
            }
            piece = clipEdge(piece, cover[i], edge, sign);
        }
        // What remains inside every edge is supported. Retain only the disjoint outside pieces.
    }
    unsupported = std::move(remaining);
}
} // namespace

void LevelGeometry::synchronize(Engine& engine, bool legacyProps) {
    const auto* scene = engine.scene();
    const auto* grid = scene ? scene->navigationGrid() : nullptr;
    scratch_.clear();
    bool imported = false;
    if (scene) {
        for (const auto& [id, object] : scene->objects()) {
            if (!object.model || !scene->enabledInHierarchy(id)) {
                continue;
            }
            const bool terrain = object.role == "grimlock.terrain";
            const bool obstacle = object.role == "grimlock.obstacle";
            if (terrain || obstacle || (legacyProps && object.role.empty())) {
                scratch_.push_back({id, *object.model, scene->worldTransform(id), object.role});
                imported = imported || terrain;
            }
        }
    }
    const auto* sourceGrid = imported ? nullptr : grid;
    const auto configuration = sourceGrid ? std::optional{sourceGrid->configuration()} : std::nullopt;
    const auto revision = sourceGrid ? std::optional{sourceGrid->revision()} : std::nullopt;
    if (scene_ == scene && grid_ == sourceGrid && configuration_ == configuration && gridRevision_ == revision &&
        objects_ == scratch_) {
        return;
    }
    NavigationGeometry geometry;
    std::vector<TerrainBox> boxes;
    if (grid && !imported) {
        boxes = terrainBoxes(*grid);
        geometry = navigationGeometry(boxes);
    }
    for (const auto& object : scratch_) {
        if (object.role.empty()) {
            const auto info = engine.modelInfo(engine.loadModel(engine.assetRoot() / object.model));
            const glm::vec3 center = object.world * glm::vec4{(info.minimum + info.maximum) * 0.5F, 1};
            const auto size = (info.maximum - info.minimum) * 0.5F;
            const auto extent = glm::abs(glm::vec3{object.world[0]}) * size.x +
                                glm::abs(glm::vec3{object.world[1]}) * size.y +
                                glm::abs(glm::vec3{object.world[2]}) * size.z;
            if (extent.y * 2 > 0.05F) {
                boxes.push_back({center, extent, false});
                geometry.addBox(center - extent, center + extent, false);
            }
            continue;
        }
        auto found = models_.find(object.model);
        if (found == models_.end()) {
            found =
                models_
                    .emplace(object.model, engine.modelTriangles(engine.loadModel(engine.assetRoot() / object.model)))
                    .first;
        }
        glm::vec3 minimum{std::numeric_limits<float>::max()}, maximum{-std::numeric_limits<float>::max()};
        for (const auto& triangle : found->second) {
            std::array<glm::vec3, 3> world;
            for (std::size_t i = 0; i < 3; ++i) {
                world[i] = object.world * glm::vec4{triangle[i], 1};
                minimum = glm::min(minimum, world[i]);
                maximum = glm::max(maximum, world[i]);
            }
            // Preserve outward winding under mirrored scene transforms.
            if (glm::determinant(glm::mat3{object.world}) < 0) {
                std::swap(world[1], world[2]);
            }
            geometry.addTriangle(world[0], world[1], world[2], object.role == "grimlock.terrain");
        }
        if (object.role == "grimlock.obstacle" && !found->second.empty()) {
            // Obstacle volumes are conservative; their shells must not leave navigable interiors.
            geometry.solidBoxes.push_back({minimum, maximum});
        }
    }
    geometry_ = std::move(geometry);
    boxes_ = std::move(boxes);
    objects_.swap(scratch_);
    scene_ = scene;
    grid_ = sourceGrid;
    configuration_ = configuration;
    gridRevision_ = revision;
    imported_ = imported;
    ++revision_;
}

std::optional<glm::dvec3> LevelGeometry::intersect(const Ray& ray) const {
    if (!std::isfinite(glm::length(ray.origin)) || !std::isfinite(glm::length(ray.direction)) ||
        !std::isfinite(ray.minimum) || !std::isfinite(ray.maximum) || ray.minimum < 0 || ray.maximum < ray.minimum) {
        return {};
    }
    double closest = ray.maximum;
    std::optional<glm::dvec3> result;
    for (std::size_t i = 0; i < geometry_.triangles.size(); i += 3) {
        const auto vertex = [&](std::size_t corner) {
            const auto index = static_cast<std::size_t>(geometry_.triangles[i + corner]) * 3;
            return glm::dvec3{geometry_.vertices[index], geometry_.vertices[index + 1], geometry_.vertices[index + 2]};
        };
        const auto a = vertex(0), ab = vertex(1) - a, ac = vertex(2) - a;
        const auto p = glm::cross(ray.direction, ac);
        const double determinant = glm::dot(ab, p);
        if (std::abs(determinant) < 1e-12) {
            continue;
        }
        const auto offset = ray.origin - a;
        const double u = glm::dot(offset, p) / determinant;
        const auto q = glm::cross(offset, ab);
        const double v = glm::dot(ray.direction, q) / determinant;
        const double distance = glm::dot(ac, q) / determinant;
        if (u >= 0 && v >= 0 && u + v <= 1 && distance >= ray.minimum && distance < closest) {
            closest = distance;
            result = ray.origin + ray.direction * distance;
        }
    }
    return result;
}

std::string LevelGeometry::supportError(const Tower& tower) const {
    // Clip every triangle to the oriented footprint. This checks its area, not just a few support samples.
    const auto inverse = glm::inverse(tower.rotation);
    const double bottom = tower.position.y - tower.halfExtent.y;
    const auto bounds = tower.bounds();
    for (const auto& solid : geometry_.solidBoxes) {
        const auto minimum = glm::vec3{bounds.center - bounds.halfExtent};
        const auto maximum = glm::vec3{bounds.center + bounds.halfExtent};
        if (glm::all(glm::lessThan(minimum, solid[1] - glm::vec3{1e-5F})) &&
            glm::all(glm::greaterThan(maximum, solid[0] + glm::vec3{1e-5F}))) {
            return "Tower overlaps an obstacle volume.";
        }
    }
    const auto extent = tower.halfExtent;
    std::vector<Polygon> unsupported{
        {{-extent.x, -extent.z}, {extent.x, -extent.z}, {extent.x, extent.z}, {-extent.x, extent.z}}};
    for (std::size_t i = 0; i < geometry_.triangles.size(); i += 3) {
        std::array<glm::dvec3, 12> polygon{}, clipped{};
        for (std::size_t j = 0; j < 3; ++j) {
            const auto index = static_cast<std::size_t>(geometry_.triangles[i + j]) * 3;
            polygon[j] =
                inverse *
                (glm::dvec3{geometry_.vertices[index], geometry_.vertices[index + 1], geometry_.vertices[index + 2]} -
                 tower.position);
        }
        const auto normal = glm::cross(polygon[1] - polygon[0], polygon[2] - polygon[0]);
        const bool upward = geometry_.walkable[i / 3] && normal.y > glm::length(normal) * std::cos(glm::radians(10.0));
        int count = 3;
        for (int plane = 0; plane < 4 && count; ++plane) {
            const int axis = plane < 2 ? 0 : 2;
            const double sign = plane % 2 ? -1 : 1;
            const double extent = tower.halfExtent[axis];
            int output = 0;
            auto previous = polygon[count - 1];
            double before = extent + sign * previous[axis];
            for (int j = 0; j < count; ++j) {
                const auto current = polygon[j];
                const double after = extent + sign * current[axis];
                if ((before >= 0) != (after >= 0)) {
                    clipped[output++] = previous + (current - previous) * (before / (before - after));
                }
                if (after >= 0) {
                    clipped[output++] = current;
                }
                previous = current;
                before = after;
            }
            polygon = clipped;
            count = output;
        }
        if (count < 3) {
            continue;
        }
        double low = std::numeric_limits<double>::max(), high = -low, area = 0;
        for (int j = 0; j < count; ++j) {
            low = std::min(low, polygon[j].y + tower.position.y);
            high = std::max(high, polygon[j].y + tower.position.y);
            const auto& next = polygon[(j + 1) % count];
            area += polygon[j].x * next.z - next.x * polygon[j].z;
        }
        area = std::abs(area) * 0.5;
        if (area < 1e-9) {
            bool boundaryContact = false;
            for (const int axis : {0, 2}) {
                for (const double side : {-1.0, 1.0}) {
                    bool onEdge = true;
                    for (int j = 0; j < count; ++j) {
                        onEdge = onEdge && std::abs(polygon[j][axis] - side * tower.halfExtent[axis]) < 1e-5;
                    }
                    boundaryContact = boundaryContact || onEdge;
                }
            }
            if (boundaryContact) {
                continue;
            }
            // Vertical faces still obstruct the tower volume.
            if (high > bottom + 0.04 && low < tower.position.y + tower.halfExtent.y - 1e-5) {
                return "Tower intersects terrain or an obstacle.";
            }
            continue;
        }
        if (upward && low >= bottom - 0.04 && high <= bottom + 0.04) {
            subtractSupport(unsupported, {polygon.data(), static_cast<std::size_t>(count)});
        } else if (high > bottom + 0.04 && low < tower.position.y + tower.halfExtent.y - 1e-5) {
            return "Tower intersects terrain or an obstacle.";
        }
    }
    double unsupportedArea = 0;
    for (const auto& piece : unsupported) {
        unsupportedArea += std::abs(signedArea(piece));
    }
    return unsupportedArea <= 1e-5
               ? std::string{}
               : "The complete footprint needs gently sloping terrain support (10 degrees, 4 cm foundation tolerance).";
}

} // namespace grimlock::game
