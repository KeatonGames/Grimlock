#include "systems/placement_system.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace grimlock::game {
namespace {

bool valid(const BuildZone& zone) {
    return std::isfinite(glm::length(zone.position)) && std::isfinite(zone.yaw) &&
           std::isfinite(glm::length(zone.dimensions)) && zone.dimensions.x > 0 && zone.dimensions.y > 0 &&
           std::isfinite(glm::length(zone.constructionOffset)) && std::isfinite(zone.constructionYaw);
}

glm::dvec3 rotate(double yaw, glm::dvec3 point) {
    return glm::angleAxis(yaw, glm::dvec3{0, 1, 0}) * point;
}

} // namespace

glm::dvec3 BuildZone::constructionPosition() const {
    return position + rotate(yaw, constructionOffset);
}

bool BuildZone::covers(glm::dvec3 foot) const {
    const auto local = rotate(-yaw, foot - position);
    return valid(*this) && std::abs(local.y - constructionOffset.y) <= 0.05 &&
           std::abs(local.x) <= dimensions.x * 0.5 + 1e-6 && std::abs(local.z) <= dimensions.y * 0.5 + 1e-6;
}

Tower BuildZone::snap(Tower tower) const {
    tower.buildZone = object;
    tower.position = constructionPosition() + glm::dvec3{0, tower.halfExtent.y, 0};
    tower.rotation = glm::angleAxis(yaw + constructionYaw, glm::dvec3{0, 1, 0});
    return tower;
}

bool BuildZone::contains(const Tower& tower) const {
    if (!valid(*this) || !std::isfinite(glm::length(tower.position)) || !std::isfinite(glm::length(tower.rotation)) ||
        std::abs(glm::length(tower.rotation) - 1) > 1e-6 ||
        glm::length(tower.rotation * glm::dvec3{0, 1, 0} - glm::dvec3{0, 1, 0}) > 1e-6 ||
        !std::isfinite(glm::length(tower.halfExtent)) ||
        glm::any(glm::lessThanEqual(tower.halfExtent, glm::dvec3{0}))) {
        return false;
    }
    if (std::abs(tower.position.y - tower.halfExtent.y - constructionPosition().y) > 0.05) {
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

void PlacementSystem::synchronize(const Scene* scene, std::span<const Tower> towers) {
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
                std::abs(glm::dot(x / width, z / depth)) > 1e-5 || std::abs(x.y) > 1e-5 || std::abs(z.y) > 1e-5 ||
                glm::dot(glm::cross(x / width, y / height), z / depth) < 1 - 1e-5) {
                error_ = "Build zones must be upright rectangles without shear: " + object.name;
                continue;
            }
            BuildZone zone{id, object.name, glm::dvec3{world[3]}, std::atan2(-x.z, x.x), {width * 2, depth * 2}};
            bool pointValid = true;
            for (const auto anchor : scene->children(id)) {
                if (scene->object(anchor).role != "grimlock.construction_anchor") {
                    continue;
                }
                for (const auto point : scene->children(anchor)) {
                    if (scene->object(point).role == "grimlock.construction_point") {
                        const glm::dmat4 pointWorld{scene->worldTransform(point)};
                        const glm::dvec3 pointX{pointWorld[0]}, pointY{pointWorld[1]}, pointZ{pointWorld[2]};
                        pointValid =
                            scene->enabledInHierarchy(point) && glm::length(pointY - glm::dvec3{0, 1, 0}) < 1e-5 &&
                            std::abs(glm::length(pointX) - 1) < 1e-5 && std::abs(glm::length(pointZ) - 1) < 1e-5 &&
                            std::abs(glm::dot(pointX, pointZ)) < 1e-5 && std::abs(pointX.y) < 1e-5 &&
                            std::abs(pointZ.y) < 1e-5 && glm::dot(glm::cross(pointX, pointY), pointZ) > 1 - 1e-5;
                        zone.constructionOffset = rotate(-zone.yaw, glm::dvec3{pointWorld[3]} - zone.position);
                        zone.constructionYaw = std::atan2(-pointWorld[0].z, pointWorld[0].x) - zone.yaw;
                        zone.constructionObject = point;
                        break;
                    }
                }
            }
            if (!pointValid || !valid(zone)) {
                error_ = "Invalid or disabled construction point/build zone (keep points upright with unit scale): " +
                         object.name;
                continue;
            }
            scratch_.push_back(std::move(zone));
        }
    }
    zones_.swap(scratch_);
    for (const auto& tower : towers) {
        auto zone = std::find_if(zones_.begin(), zones_.end(), [&](const BuildZone& item) {
            return tower.buildZone ? item.object == tower.buildZone
                                   : item.covers(tower.position - glm::dvec3{0, tower.halfExtent.y, 0});
        });
        if (zone != zones_.end() && tower.entity) {
            zone->occupants.push_back(tower.entity);
        }
    }
    for (const auto& zone : zones_) {
        if (zone.occupants.size() > 1) {
            error_ += "Legacy zone '" + zone.name + "' contains " + std::to_string(zone.occupants.size()) +
                      " towers. Retained in place; remove or relocate extras in the Editor. ";
        }
    }
}

const BuildZone* PlacementSystem::zoneAt(glm::dvec3 foot) const {
    const auto found = std::find_if(zones_.begin(), zones_.end(), [&](const BuildZone& zone) {
        return zone.covers(foot);
    });
    return found == zones_.end() ? nullptr : &*found;
}

std::string PlacementSystem::check(const Tower& tower) const {
    const auto zone = tower.buildZone ? std::find_if(zones_.begin(),
                                                     zones_.end(),
                                                     [&](const BuildZone& item) {
                                                         return item.object == tower.buildZone;
                                                     })
                                      : std::find_if(zones_.begin(), zones_.end(), [&](const BuildZone& item) {
                                            return item.covers(tower.position - glm::dvec3{0, tower.halfExtent.y, 0});
                                        });
    if (zone == zones_.end()) {
        return "Select an available build zone.";
    }
    if (!zone->occupants.empty()) {
        return "This build zone is occupied.";
    }
    const auto snapped = zone->snap(tower);
    if (glm::length(snapped.position - tower.position) > 1e-5 ||
        std::min(glm::length(snapped.rotation - tower.rotation), glm::length(snapped.rotation + tower.rotation)) >
            1e-5) {
        return "Build at the zone's designated construction point and facing.";
    }
    return zone->contains(tower) ? std::string{} : "The tower footprint must fit inside its build zone.";
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
    (void)ensureConstructionPoint(scene, zone);
}

ObjectId PlacementSystem::ensureConstructionPoint(Scene& scene, const BuildZone& zone) {
    if (!valid(zone) || !scene.contains(zone.object) || scene.object(zone.object).role != zoneRole) {
        throw std::invalid_argument("Construction points require a valid build zone.");
    }
    ObjectId anchor = 0, point = 0;
    for (const auto child : scene.children(zone.object)) {
        if (scene.object(child).role == "grimlock.construction_anchor") {
            anchor = child;
            break;
        }
    }
    if (!anchor) {
        anchor = scene.createObject("Construction anchor", zone.object);
        scene.setRole(anchor, "grimlock.construction_anchor");
    }
    for (const auto child : scene.children(anchor)) {
        if (scene.object(child).role == "grimlock.construction_point") {
            point = child;
            break;
        }
    }
    if (!point) {
        point = scene.createObject("Construction point", anchor);
        scene.setRole(point, "grimlock.construction_point");
    }
    // Cancel zone dimensions before applying facing, avoiding shear on rotated towers.
    const glm::dmat4 world{scene.worldTransform(zone.object)};
    const glm::dvec3 scale{glm::length(glm::dvec3{world[0]}),
                           glm::length(glm::dvec3{world[1]}),
                           glm::length(glm::dvec3{world[2]})};
    Transform anchorTransform;
    anchorTransform.position = zone.constructionOffset / scale;
    anchorTransform.scale = glm::dvec3{1} / scale;
    scene.setTransform(anchor, anchorTransform);
    Transform facing;
    facing.rotation = glm::angleAxis(zone.constructionYaw, glm::dvec3{0, 1, 0});
    scene.setTransform(point, facing);
    return point;
}

} // namespace grimlock::game
