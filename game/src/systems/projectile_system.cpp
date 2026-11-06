#include "systems/projectile_system.hpp"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <stdexcept>

namespace grimlock::game {
namespace {
glm::mat4 projectileTransform(const Projectile& projectile) {
    glm::mat4 transform{1};
    if (glm::length(projectile.velocity) > 1e-8) {
        const glm::vec3 forward = glm::normalize(glm::vec3{projectile.velocity});
        const auto reference = std::abs(forward.y) > 0.99F ? glm::vec3{0, 0, 1} : glm::vec3{0, 1, 0};
        const auto right = glm::normalize(glm::cross(reference, forward));
        transform[0] = glm::vec4{right, 0};
        transform[1] = glm::vec4{glm::cross(forward, right), 0};
        transform[2] = glm::vec4{forward, 0};
    }
    transform[3] = glm::vec4{projectile.position, 1};
    const auto scale = projectile.visualScale > 0 ? projectile.visualScale : static_cast<float>(projectile.radius);
    return glm::scale(transform, glm::vec3{scale});
}
} // namespace

ProjectileSystem::~ProjectileSystem() {
    clear();
}

void ProjectileSystem::spawn(glm::dvec3 origin,
                             EnemyId target,
                             const TowerDefinition& definition,
                             std::size_t level,
                             ObjectId source,
                             double lifetime,
                             ecs::Entity sourceEntity) {
    const auto& statistics = definition.statistics(level);
    spawn(origin,
          target,
          ProjectileDefinition{statistics.damage,
                               definition.projectileSpeed,
                               definition.projectileRadius,
                               definition.projectileTint},
          source,
          {DefensiveTargetKind::tower, sourceEntity},
          lifetime);
}

void ProjectileSystem::spawn(glm::dvec3 origin,
                             EnemyId target,
                             const ProjectileDefinition& definition,
                             ObjectId source,
                             DefensiveTarget attacker,
                             double lifetime) {
    if (!std::isfinite(glm::length(origin)) || !std::isfinite(definition.damage) || definition.damage <= 0 ||
        !std::isfinite(definition.speed) || definition.speed <= 0 || !std::isfinite(definition.radius) ||
        definition.radius <= 0 || !std::isfinite(definition.splashRadius) || definition.splashRadius < 0 ||
        !std::isfinite(definition.visualScale) || definition.visualScale < 0 || std::isnan(lifetime) || lifetime <= 0) {
        throw std::invalid_argument("Invalid projectile origin or statistics.");
    }
    if (!model_) {
        model_ = engine_.loadModel(engine_.assetRoot() / "towers/structures/primitives/cube.glb");
    }
    const auto entity = registry_.create();
    std::optional<InstanceId> instance;
    try {
        const auto model =
            definition.model.empty() ? *model_ : engine_.loadModel(engine_.assetRoot() / definition.model);
        instance = engine_.createInstance(model, true);
        engine_.setTint(*instance, definition.tint);
        registry_.emplace<Projectile>(
            entity,
            Projectile{target, origin, definition.speed, definition.radius, definition.damage, *instance, source});
        registry_.emplace<ProjectileLifetimeComponent>(entity, ProjectileLifetimeComponent{0, lifetime});
        projectiles_.back().sourceEntity = attacker.id;
        projectiles_.back().sourceKind = attacker.kind;
        projectiles_.back().splashRadius = definition.splashRadius;
        projectiles_.back().visualScale = definition.visualScale;
        engine_.setTransform(*instance, projectileTransform(projectiles_.back()));
    } catch (...) {
        if (instance) {
            engine_.removeInstance(*instance);
        }
        registry_.destroy(entity);
        throw;
    }
}

void ProjectileSystem::update(EnemySystem& enemies, double delta, const CombatGeometry* geometry) {
    if (!std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Projectile delta must be finite and nonnegative.");
    }
    if (!geometry) {
        geometry_.synchronize(engine_);
        geometry = &geometry_;
    }
    for (std::size_t index = 0; index < projectiles_.size();) {
        auto& projectile = projectiles_[index];
        auto& lifetime = *lifetimes_.get(projectiles_.entities()[index]);
        lifetime.elapsed += delta;
        if (std::isfinite(lifetime.maximum) && lifetime.elapsed >= lifetime.maximum) {
            removeAt(index);
            continue;
        }
        const auto* target = enemies.find(projectile.target);
        if (!target || !target->active || !target->alive) {
            removeAt(index);
            continue;
        }
        const auto center = combatAim(*target);
        const auto offset = center - projectile.position;
        const auto distance = glm::length(offset);
        projectile.velocity = distance > 1e-10 ? offset * (projectile.speed / distance) : glm::dvec3{0};
        const auto travel = projectile.speed * delta;
        const double hitDistance = std::max(0.0, distance - projectile.radius - target->radius);
        const double segmentLength = std::min(travel, hitDistance);
        const auto next =
            distance > 1e-10 ? projectile.position + offset * (segmentLength / distance) : projectile.position;
        if (geometry->trace(projectile.position, next, projectile.radius, projectile.source)) {
            removeAt(index);
            continue;
        }
        // Sweeping the entire homing segment prevents tunneling, even when one frame crosses the target.
        if (distance <= travel + projectile.radius + target->radius) {
            const DefensiveTarget attacker = projectile.sourceEntity
                                                 ? DefensiveTarget{projectile.sourceKind, projectile.sourceEntity}
                                                 : DefensiveTarget{};
            victims_.clear();
            if (projectile.splashRadius > 0) {
                enemies.visitNearby(center, projectile.splashRadius, [&](const Enemy& enemy) {
                    if (enemy.active && enemy.alive &&
                        glm::length(combatAim(enemy) - center) <= projectile.splashRadius &&
                        geometry->visible(center, combatAim(enemy))) {
                        victims_.push_back(enemy.id);
                    }
                });
            } else {
                victims_.push_back(projectile.target);
            }
            bool hit = false;
            // Collect stable IDs before lethal damage compacts enemy storage.
            for (const auto victim : victims_) {
                hit = enemies.applyDamage(victim, projectile.damage, attacker) || hit;
            }
            if (hit) {
                ++impacts_;
            }
            removeAt(index);
            continue;
        }
        projectile.position = next;
        engine_.setTransform(projectile.instance, projectileTransform(projectile));
        ++index;
    }
}

void ProjectileSystem::removeAt(std::size_t index) noexcept {
    const auto entity = projectiles_.entities()[index];
    engine_.removeInstance(projectiles_[index].instance);
    registry_.destroy(entity);
}

void ProjectileSystem::clear() noexcept {
    while (!projectiles_.empty()) {
        removeAt(projectiles_.size() - 1);
    }
    impacts_ = 0;
}

} // namespace grimlock::game
