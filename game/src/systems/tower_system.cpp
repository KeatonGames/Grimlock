#include "systems/tower_system.hpp"

#include "navigation/level_geometry.hpp"
#include "systems/health.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace grimlock::game {

namespace {

constexpr const char* towerRole = "grimlock.tower";

bool overlap(const TerrainBox& a, const TerrainBox& b) {
    const auto distance = glm::abs(a.center - b.center);
    const auto extent = a.halfExtent + b.halfExtent - glm::vec3{1e-5F};
    return distance.x < extent.x && distance.y < extent.y && distance.z < extent.z;
}

std::optional<double> intersect(const Ray& ray, const TerrainBox& box) {
    const auto minimum = glm::dvec3{box.center - box.halfExtent};
    const auto maximum = glm::dvec3{box.center + box.halfExtent};
    double near = ray.minimum, far = ray.maximum;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(ray.direction[axis]) < 1e-12) {
            if (ray.origin[axis] < minimum[axis] || ray.origin[axis] > maximum[axis]) {
                return {};
            }
            continue;
        }
        const auto a = (minimum[axis] - ray.origin[axis]) / ray.direction[axis];
        const auto b = (maximum[axis] - ray.origin[axis]) / ray.direction[axis];
        near = std::max(near, std::min(a, b));
        far = std::min(far, std::max(a, b));
        if (near > far) {
            return {};
        }
    }
    return near;
}

} // namespace

TerrainBox Tower::bounds() const {
    const auto axes = glm::mat3_cast(rotation);
    const auto extent =
        glm::abs(axes[0]) * halfExtent.x + glm::abs(axes[1]) * halfExtent.y + glm::abs(axes[2]) * halfExtent.z;
    return {glm::vec3{position}, glm::vec3{extent}, false};
}

std::optional<ObjectId> TowerSystem::pick(const Ray& ray) const {
    std::optional<ObjectId> selected;
    double closest = ray.maximum;
    for (const auto& tower : towers_) {
        if (const auto hit = intersect(ray, tower.bounds()); hit && *hit <= closest) {
            selected = tower.object;
            closest = *hit;
        }
    }
    return selected;
}

void TowerSystem::clear() noexcept {
    scene_ = nullptr;
    towers_.clear();
    scratchTowers_.clear();
    props_.clear();
    models_.clear();
    progress_.clear();
    catalogLoaded_ = false;
    ++revision_;
}

void TowerSystem::synchronize(Engine& engine) {
    const auto* scene = engine.scene();
    if (scene != scene_) {
        clear();
        scene_ = scene;
    }
    if (!scene) {
        return;
    }
    if (!catalogLoaded_) {
        definitions_ = loadTowerDefinitions(engine.assetRoot());
        catalogLoaded_ = true;
    }
    scratchTowers_.clear();
    props_.clear();
    for (const auto& [id, object] : scene->objects()) {
        if (!object.model || !scene->enabledInHierarchy(id) || object.role == "grimlock.hero" ||
            object.role == "grimlock.terrain" || object.role == "grimlock.decoration" ||
            object.role == "grimlock.obstacle") {
            continue;
        }
        const auto world = glm::dmat4{scene->worldTransform(id)};
        if (object.role == towerRole || object.role.starts_with("grimlock.tower:")) {
            const auto type = object.role == towerRole ? "basic" : object.role.substr(15);
            Tower tower = definition(type).instance();
            const auto maximum = definition(type).maximumHealth;
            auto& progress = progress_.try_emplace(id, TowerProgress{type, 1, maximum, maximum}).first->second;
            if (progress.type != type) {
                progress = TowerProgress{type, 1, maximum, maximum};
            }
            tower.object = id;
            tower.position = world[3];
            glm::dmat3 orientation{world};
            for (int axis = 0; axis < 3; ++axis) {
                const double length = glm::length(orientation[axis]);
                orientation[axis] = length > 1e-10 ? orientation[axis] / length : glm::dmat3{1}[axis];
            }
            tower.rotation = glm::normalize(glm::quat_cast(orientation));
            tower.model = *object.model;
            tower.visualScale = {glm::length(glm::dvec3{world[0]}),
                                 glm::length(glm::dvec3{world[1]}),
                                 glm::length(glm::dvec3{world[2]})};
            scratchTowers_.push_back(std::move(tower));
        } else {
            auto found = models_.find(*object.model);
            if (found == models_.end()) {
                found =
                    models_
                        .emplace(*object.model, engine.modelInfo(engine.loadModel(engine.assetRoot() / *object.model)))
                        .first;
            }
            const auto& info = found->second;
            const glm::dvec3 center = world * glm::dvec4{(info.minimum + info.maximum) * 0.5F, 1};
            const auto size = glm::dvec3{info.maximum - info.minimum} * 0.5;
            const auto extent = glm::abs(glm::dvec3{world[0]}) * size.x + glm::abs(glm::dvec3{world[1]}) * size.y +
                                glm::abs(glm::dvec3{world[2]}) * size.z;
            // Flat spawn/objective markers are floor decoration, not placement obstacles.
            if (extent.y * 2 > 0.05) {
                props_.push_back({glm::vec3{center}, glm::vec3{extent}, false});
            }
        }
    }
    const bool sameObstacles =
        scratchTowers_.size() == towers_.size() &&
        std::equal(scratchTowers_.begin(), scratchTowers_.end(), towers_.begin(), [](const Tower& a, const Tower& b) {
            return a.object == b.object && a.position == b.position && a.halfExtent == b.halfExtent &&
                   std::min(glm::length(a.rotation - b.rotation), glm::length(a.rotation + b.rotation)) < 1e-6;
        });
    towers_.swap(scratchTowers_);
    std::erase_if(progress_, [&](const auto& entry) {
        const auto id = entry.first;
        if (!scene->contains(id)) {
            return true;
        }
        const auto& role = scene->object(id).role;
        return role != towerRole && !role.starts_with("grimlock.tower:");
    });
    if (!sameObstacles) {
        ++revision_;
    }
}

ObjectId TowerSystem::create(Engine& engine, const Tower& tower) {
    if (!engine.scene()) {
        throw std::logic_error("Tower creation requires an active scene.");
    }
    synchronize(engine);
    auto& scene = *engine.scene();
    const auto& configured = definition(tower.type);
    const auto id = scene.createObject(configured.name);
    try {
        scene.setRole(id, tower.type == "basic" ? towerRole : "grimlock.tower:" + tower.type);
        scene.setModel(id, tower.model);
        Transform transform;
        transform.position = tower.position;
        transform.rotation = tower.rotation;
        transform.scale = tower.visualScale;
        scene.setTransform(id, transform);
        scene.setTint(id, configured.tint);
        engine.prepareScene(scene);
        synchronize(engine);
        scene.selectObject(id);
    } catch (...) {
        scene.removeObject(id);
        throw;
    }
    return id;
}

bool TowerSystem::remove(Engine& engine, ObjectId object) {
    synchronize(engine);
    if (!contains(object)) {
        return false;
    }
    engine.scene()->removeObject(object);
    synchronize(engine);
    return true;
}

bool TowerSystem::contains(ObjectId object) const noexcept {
    return std::any_of(towers_.begin(), towers_.end(), [object](const Tower& tower) {
        return tower.object == object;
    });
}

const TowerDefinition& TowerSystem::definition(const std::string& type) const {
    const auto found = std::find_if(definitions_.begin(), definitions_.end(), [&](const TowerDefinition& entry) {
        return entry.id == type;
    });
    if (found == definitions_.end()) {
        throw std::invalid_argument("Unknown tower type: " + type);
    }
    return *found;
}

const TowerProgress& TowerSystem::progress(ObjectId object) const {
    return progress_.at(object);
}

void TowerSystem::upgrade(ObjectId object, EconomySystem& economy) {
    if (!contains(object)) {
        throw std::invalid_argument("Select an existing tower to upgrade.");
    }
    auto& progress = progress_.at(object);
    const auto& configured = definition(progress.type);
    if (progress.level >= configured.levels.size()) {
        throw std::invalid_argument("MAX LEVEL");
    }
    const auto cost = configured.statistics(progress.level + 1).upgradeCost;
    if (!economy.trySpend(cost)) {
        throw std::invalid_argument("Insufficient gold to upgrade this tower.");
    }
    // All validation precedes spending; the remaining state change cannot fail.
    ++progress.level;
}

void TowerSystem::resetUpgrades() noexcept {
    for (auto& [id, progress] : progress_) {
        progress.level = 1;
        progress.health = progress.maximumHealth;
        progress.hitFlashRemaining = 0;
    }
}

bool TowerSystem::applyDamage(Engine& engine, ObjectId object, float damage) {
    validateDamage(damage);
    const auto found = progress_.find(object);
    if (found == progress_.end() || !engine.scene() || !engine.scene()->contains(object) || damage == 0) {
        return false;
    }
    if (receiveDamage(found->second.health, damage)) {
        engine.scene()->removeObject(object);
        // One navigation synchronization after all impacts batches obstacle rebuilds.
        progress_.erase(found);
        std::erase_if(towers_, [object](const Tower& tower) {
            return tower.object == object;
        });
        ++revision_;
    } else {
        found->second.hitFlashRemaining = 0.15;
        engine.scene()->setTint(object, {1, 0.35F, 0.3F, 1});
    }
    return true;
}

void TowerSystem::updateEffects(Engine& engine, double delta) {
    for (auto& [object, progress] : progress_) {
        if (progress.hitFlashRemaining > 0) {
            progress.hitFlashRemaining = std::max(0.0, progress.hitFlashRemaining - delta);
            if (progress.hitFlashRemaining == 0 && engine.scene()->contains(object)) {
                engine.scene()->setTint(object, definition(progress.type).tint);
            }
        }
    }
}

std::string TowerSystem::check(const Tower& tower,
                               std::span<const TerrainBox> terrain,
                               std::span<const Enemy> enemies,
                               const LevelGeometry* geometry) const {
    const auto& configured = definition(tower.type);
    if (!std::isfinite(glm::length(tower.position)) || !std::isfinite(glm::length(tower.halfExtent)) ||
        tower.halfExtent != configured.halfExtent || tower.model != configured.model ||
        !std::isfinite(glm::length(tower.visualScale)) ||
        glm::any(glm::lessThanEqual(tower.visualScale, glm::dvec3{0})) || !std::isfinite(glm::length(tower.rotation)) ||
        std::abs(glm::length(tower.rotation) - 1) > 1e-6 ||
        glm::length(tower.rotation * glm::dvec3{0, 1, 0} - glm::dvec3{0, 1, 0}) > 1e-6) {
        return "Placement requires a finite upright tower with the configured footprint.";
    }
    const auto box = tower.bounds();
    const auto minimum = box.center - box.halfExtent, maximum = box.center + box.halfExtent;
    double supportedArea = 0;
    for (const auto& surface : terrain) {
        if (!surface.walkableTop) {
            if (overlap(box, surface)) {
                return "Tower overlaps a wall.";
            }
            continue;
        }
        const auto a = surface.center - surface.halfExtent, b = surface.center + surface.halfExtent;
        if (std::abs(b.y - minimum.y) < 1e-4F) {
            supportedArea += std::max(0.0F, std::min(maximum.x, b.x) - std::max(minimum.x, a.x)) *
                             std::max(0.0F, std::min(maximum.z, b.z) - std::max(minimum.z, a.z));
        }
    }
    if (geometry) {
        if (const auto error = geometry->supportError(tower); !error.empty()) {
            return error;
        }
    } else if (supportedArea + 1e-4 < 4 * box.halfExtent.x * box.halfExtent.z) {
        return "The entire footprint must rest on terrain.";
    }
    for (const auto& existing : towers_) {
        if (overlap(box, existing.bounds())) {
            return "Tower overlaps another tower.";
        }
    }
    for (const auto& prop : props_) {
        if (overlap(box, prop)) {
            return "Tower overlaps a scene model.";
        }
    }
    for (const auto& enemy : enemies) {
        if (!enemy.active || !enemy.alive) {
            continue;
        }
        const auto nearest = glm::clamp(glm::dvec2{enemy.position.x, enemy.position.z},
                                        glm::dvec2{minimum.x, minimum.z},
                                        glm::dvec2{maximum.x, maximum.z});
        if (glm::length(nearest - glm::dvec2{enemy.position.x, enemy.position.z}) < enemy.radius + 1e-4 &&
            enemy.position.y + enemy.height > minimum.y && enemy.position.y < maximum.y) {
            return "Tower overlaps an animal.";
        }
    }
    return {};
}

std::optional<glm::dvec3> TowerSystem::terrainHit(const Ray& ray, std::span<const TerrainBox> terrain) {
    if (!std::isfinite(glm::length(ray.origin)) || !std::isfinite(glm::length(ray.direction)) ||
        !std::isfinite(ray.minimum) || !std::isfinite(ray.maximum) || ray.minimum < 0 || ray.maximum < ray.minimum ||
        glm::length(ray.direction) < 1e-10) {
        return {};
    }
    std::optional<double> closest;
    for (const auto& box : terrain) {
        const auto hit = intersect(ray, box);
        if (hit && (!closest || *hit < *closest)) {
            closest = hit;
        }
    }
    return closest ? std::optional{ray.origin + ray.direction * *closest} : std::nullopt;
}

bool TowerSystem::routeFromArea(const NavMesh& navigation, const SpawnArea& area, glm::dvec3 destination) {
    if (!std::isfinite(glm::length(area.minimum)) || !std::isfinite(glm::length(area.maximum)) ||
        !std::isfinite(area.height) || area.minimum.x > area.maximum.x || area.minimum.y > area.maximum.y) {
        return false;
    }
    NavigationPath path;
    for (const auto& triangle : navigation.debugTriangles()) {
        std::array<glm::dvec3, 16> polygon{}, clipped{};
        std::copy(triangle.begin(), triangle.end(), polygon.begin());
        int count = 3;
        for (int plane = 0; plane < 4 && count; ++plane) {
            const int axis = plane < 2 ? 0 : 2;
            const double edge = plane == 0   ? area.minimum.x
                                : plane == 1 ? area.maximum.x
                                : plane == 2 ? area.minimum.y
                                             : area.maximum.y;
            const double sign = plane % 2 == 0 ? 1 : -1;
            int output = 0;
            auto previous = polygon[count - 1];
            double before = sign * (previous[axis] - edge);
            for (int i = 0; i < count; ++i) {
                const auto current = polygon[i];
                const double after = sign * (current[axis] - edge);
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
        if (!count) {
            continue;
        }
        glm::dvec3 center{0};
        for (int i = 0; i < count; ++i) {
            center += polygon[i];
        }
        center /= count;
        const auto start = navigation.project({center.x, area.height, center.z}, 0.03);
        if (start && start->position.x >= area.minimum.x - 1e-5 && start->position.x <= area.maximum.x + 1e-5 &&
            start->position.z >= area.minimum.y - 1e-5 && start->position.z <= area.maximum.y + 1e-5 &&
            navigation.contains({center.x, area.height, center.z}, 0.03) &&
            navigation.findPath(start->position, destination, path)) {
            return true;
        }
    }
    return false;
}

} // namespace grimlock::game
