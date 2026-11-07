#include "systems/combat_system.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace grimlock::game {

void CombatSystem::update(TowerSystem& towers, EnemySystem& enemies, double delta, const CombatGeometry* geometry) {
    if (!std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Combat delta must be finite and nonnegative.");
    }
    const auto instances = towers.towers();
    if (!geometry) {
        geometry_.synchronize(engine_, instances);
        geometry = &geometry_;
    }
    if (resetAttacks_) {
        towers.resetCombat();
        resetAttacks_ = false;
    }
    // Bounded steps preserve the configured cadence instead of dropping shots on slow frames.
    while (delta > 1e-10) {
        const double step = std::min(delta, 1.0 / 60);
        for (std::size_t index = 0; index < instances.size(); ++index) {
            const auto& tower = instances[index];
            auto& attack = towers.attack(tower.object);
            const auto& definition = towers.definition(tower.type);
            const auto level = towers.progress(tower.object).level;
            const auto& statistics = definition.statistics(level);
            attack.cooldown = std::min(attack.cooldown, 1.0 / statistics.fireRate);
            const Enemy* nearest = nullptr;
            const auto origin = firingOrigin(tower);
            const auto base = tower.position - glm::dvec3{0, tower.halfExtent.y, 0};
            const auto valid = [&](const Enemy& enemy) {
                const auto offset = enemy.position - base;
                return enemies.canDamage(enemy, DamageType::physical) &&
                       hostile(towers.team(tower.entity), enemies.team(enemy.id)) &&
                       glm::dot(offset, offset) <= statistics.attackRange * statistics.attackRange &&
                       !geometry->trace(origin, combatAim(enemy), definition.projectileRadius, tower.object);
            };
            if (attack.target) {
                const auto* current = enemies.find(*attack.target);
                if (current && valid(*current)) {
                    nearest = current;
                }
            }
            double nearestDistance = statistics.attackRange * statistics.attackRange;
            attack.searchDelay = std::max(0.0, attack.searchDelay - step);
            const auto* tracked = nearest;
            const bool rescan = (attack.target && !tracked) || attack.searchDelay <= 0 || attack.level != level ||
                                attack.geometryRevision != geometry->revision() ||
                                attack.enemyCount != enemies.enemies().size();
            if (rescan) {
                nearest = nullptr;
                enemies.visitNearby(base, statistics.attackRange, [&](const Enemy& enemy) {
                    // Range uses terrain-level entity anchors, independent of either model's visual height.
                    const auto offset = enemy.position - base;
                    const double distance = glm::dot(offset, offset);
                    if (enemy.active && enemy.alive && distance <= nearestDistance &&
                        (!nearest || distance < nearestDistance || enemyOrder(enemy) < enemyOrder(*nearest)) &&
                        valid(enemy)) {
                        nearest = &enemy;
                        nearestDistance = distance;
                    }
                });
                if (tracked && nearest) {
                    const double oldDistance = glm::length(tracked->position - base);
                    const double newDistance = glm::length(nearest->position - base);
                    // Keep near-equal targets stable while still reacting to a meaningfully closer threat.
                    if (oldDistance - newDistance <= tracked->radius + nearest->radius) {
                        nearest = tracked;
                    }
                }
                attack.searchDelay = 0.15;
            }
            attack.level = level;
            attack.geometryRevision = geometry->revision();
            attack.enemyCount = enemies.enemies().size();
            attack.target = nearest ? std::optional{nearest->id} : std::nullopt;
            if (attack.target && attack.cooldown <= 1e-10) {
                projectiles_.spawn(origin,
                                   *attack.target,
                                   definition,
                                   level,
                                   tower.object,
                                   std::numeric_limits<double>::infinity(),
                                   tower.entity);
                attack.cooldown += 1.0 / statistics.fireRate;
                ++shotsFired_;
            }
            attack.cooldown -= step;
            if (!attack.target) {
                attack.cooldown = std::max(0.0, attack.cooldown);
            }
        }
        projectiles_.update(enemies, step, geometry);
        for (auto& attack : towers.attacks_) {
            if (attack.target && !enemies.find(*attack.target)) {
                attack.target.reset();
            }
        }
        delta -= step;
    }
}

void CombatSystem::clear() noexcept {
    projectiles_.clear();
    resetAttacks_ = true;
    shotsFired_ = 0;
}

} // namespace grimlock::game
