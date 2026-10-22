#include "systems/placement_system.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace grimlock::game {
namespace {

bool valid(const BuildZone& zone) {
    return std::isfinite(glm::length(zone.position)) && std::isfinite(zone.yaw) &&
           std::isfinite(glm::length(zone.dimensions)) && zone.dimensions.x > 0 && zone.dimensions.y > 0;
}

glm::dvec3 rotate(double yaw, glm::dvec3 point) {
    return glm::angleAxis(yaw, glm::dvec3{0, 1, 0}) * point;
}

} // namespace

bool BuildZone::contains(const Tower& tower) const {
    if (!valid(*this) || !std::isfinite(glm::length(tower.position)) || !std::isfinite(glm::length(tower.rotation)) ||
        std::abs(glm::length(tower.rotation) - 1) > 1e-6 ||
        glm::length(tower.rotation * glm::dvec3{0, 1, 0} - glm::dvec3{0, 1, 0}) > 1e-6 ||
        !std::isfinite(glm::length(tower.halfExtent)) ||
        glm::any(glm::lessThanEqual(tower.halfExtent, glm::dvec3{0}))) {
        return false;
    }
    if (std::abs(tower.position.y - tower.halfExtent.y - position.y) > 0.05) {
        return false;
    }
    for (const double x : {-tower.halfExtent.x, tower.halfExtent.x}) {
        for (const double z : {-tower.halfExtent.z, tower.halfExtent.z}) {
            const auto corner = tower.position + tower.rotation * glm::dvec3{x, 0, z};
            const auto local = rotate(-yaw, corner - position);
            if (std::abs(local.x) > dimensions.x * 0.5 + 1e-6 || std::abs(local.z) > dimensions.y * 0.5 + 1e-6) {
                return false;
            }
        }
    }
    return true;
}

std::array<glm::vec3, 4> BuildZone::corners() const {
    std::array<glm::vec3, 4> result;
    const std::array<glm::dvec2, 4> signs{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
    for (std::size_t i = 0; i < signs.size(); ++i) {
        result[i] = position + rotate(yaw, {signs[i].x * dimensions.x * 0.5, 0.035, signs[i].y * dimensions.y * 0.5});
    }
    return result;
}

void PlacementSystem::synchronize(const Scene* scene) {
    scratch_.clear();
    error_.clear();
    if (scene) {
        for (const auto& [id, object] : scene->objects()) {
            if (object.role != zoneRole || !scene->enabledInHierarchy(id)) {
                continue;
            }
            const glm::dmat4 world{scene->worldTransform(id)};
            const glm::dvec3 x{world[0]}, y{world[1]}, z{world[2]};
            const auto width = glm::length(x), depth = glm::length(z), height = glm::length(y);
            if (width <= 1e-8 || depth <= 1e-8 || height <= 1e-8 ||
                glm::length(y / height - glm::dvec3{0, 1, 0}) > 1e-5 ||
                std::abs(glm::dot(x / width, z / depth)) > 1e-5 || std::abs(x.y) > 1e-5 || std::abs(z.y) > 1e-5) {
                error_ = "Build zones must be upright rectangles without shear: " + object.name;
                continue;
            }
            BuildZone zone{id, object.name, glm::dvec3{world[3]}, std::atan2(-x.z, x.x), {width * 2, depth * 2}};
            if (!valid(zone)) {
                error_ = "Invalid build zone: " + object.name;
                continue;
            }
            scratch_.push_back(std::move(zone));
        }
    }
    zones_.swap(scratch_);
}

std::string PlacementSystem::check(const Tower& tower) const {
    return std::any_of(zones_.begin(),
                       zones_.end(),
                       [&](const BuildZone& zone) {
                           return zone.contains(tower);
                       })
               ? std::string{}
               : "The entire tower footprint must fit inside one build zone at this elevation.";
}

ObjectId PlacementSystem::createZone(Scene& scene, const BuildZone& zone) {
    if (!valid(zone)) {
        throw std::invalid_argument("Invalid build zone dimensions or position.");
    }
    const auto id = scene.createObject(zone.name.empty() ? "Build zone" : zone.name);
    try {
        scene.setRole(id, zoneRole);
        auto created = zone;
        created.object = id;
        if (created.name.empty()) {
            created.name = "Build zone";
        }
        editZone(scene, created);
        scene.selectObject(id, true);
    } catch (...) {
        scene.removeObject(id);
        throw;
    }
    return id;
}

void PlacementSystem::editZone(Scene& scene, const BuildZone& zone) {
    if (!valid(zone) || scene.object(zone.object).role != zoneRole || scene.object(zone.object).parent) {
        throw std::invalid_argument("Edit a finite root build zone with positive dimensions.");
    }
    Transform transform;
    transform.position = zone.position;
    transform.rotation = glm::angleAxis(zone.yaw, glm::dvec3{0, 1, 0});
    transform.scale = {zone.dimensions.x * 0.5, 1, zone.dimensions.y * 0.5};
    scene.setTransform(zone.object, transform);
    scene.setName(zone.object, zone.name);
}

} // namespace grimlock::game
