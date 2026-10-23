#include "systems/projectile_system.hpp"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <stdexcept>

namespace grimlock::game {

ProjectileSystem::~ProjectileSystem() {
    clear();
}

void ProjectileSystem::spawn(glm::dvec3 origin, EnemyId target, const TowerDefinition& definition, std::size_t level) {
    const auto& statistics = definition.statistics(level);
    if (!std::isfinite(glm::length(origin)) || !std::isfinite(statistics.damage) || statistics.damage <= 0 ||
        !std::isfinite(definition.projectileSpeed) || definition.projectileSpeed <= 0 ||
        !std::isfinite(definition.projectileRadius) || definition.projectileRadius <= 0) {
        throw std::invalid_argument("Invalid projectile origin or statistics.");
    }
    if (!model_) {
        model_ = engine_.loadModel(engine_.assetRoot() / "models/cube.obj");
    }
    const auto instance = engine_.createInstance(*model_, true);
    try {
        engine_.setTint(instance, definition.projectileTint);
        engine_.setTransform(instance,
                             glm::translate(glm::mat4{1}, glm::vec3{origin}) *
                                 glm::scale(glm::mat4{1}, glm::vec3{static_cast<float>(definition.projectileRadius)}));
        projectiles_.push_back(
            {target, origin, definition.projectileSpeed, definition.projectileRadius, statistics.damage, instance});
    } catch (...) {
        engine_.removeInstance(instance);
        throw;
    }
}

void ProjectileSystem::update(EnemySystem& enemies, double delta) {
    if (!std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Projectile delta must be finite and nonnegative.");
    }
    for (std::size_t index = 0; index < projectiles_.size();) {
        auto& projectile = projectiles_[index];
        const auto* target = enemies.find(projectile.target);
        if (!target || !target->active || !target->alive) {
            removeAt(index);
            continue;
        }
        const auto center = target->position + glm::dvec3{0, enemies.definition(target->type).visualHeight * 0.5, 0};
        const auto offset = center - projectile.position;
        const auto distance = glm::length(offset);
        const auto travel = projectile.speed * delta;
        // Sweeping the entire homing segment prevents tunneling, even when one frame crosses the target.
        if (distance <= travel + projectile.radius + target->radius) {
            if (enemies.applyDamage(projectile.target, projectile.damage)) {
                ++impacts_;
            }
            removeAt(index);
            continue;
        }
        projectile.position += offset * (travel / distance);
        engine_.setTransform(projectile.instance,
                             glm::translate(glm::mat4{1}, glm::vec3{projectile.position}) *
                                 glm::scale(glm::mat4{1}, glm::vec3{static_cast<float>(projectile.radius)}));
        ++index;
    }
}

void ProjectileSystem::removeAt(std::size_t index) noexcept {
    engine_.removeInstance(projectiles_[index].instance);
    projectiles_[index] = projectiles_.back();
    projectiles_.pop_back();
}

void ProjectileSystem::clear() noexcept {
    while (!projectiles_.empty()) {
        removeAt(projectiles_.size() - 1);
    }
    impacts_ = 0;
}

} // namespace grimlock::game
