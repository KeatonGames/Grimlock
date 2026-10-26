#include "systems/combat_system.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace grimlock::game {

void CombatSystem::update(const TowerSystem& towers,
                          EnemySystem& enemies,
                          double delta,
                          const CombatGeometry* geometry) {
    if (!std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Combat delta must be finite and nonnegative.");
    }
    const auto instances = towers.towers();
    if (!geometry) {
        geometry_.synchronize(engine_, instances);
        geometry = &geometry_;
    }
    std::erase_if(attacks_, [&](const TowerAttack& attack) {
        return std::none_of(instances.begin(), instances.end(), [&](const Tower& tower) {
            return tower.object == attack.tower;
        });
    });
    for (const auto& tower : instances) {
        if (std::none_of(attacks_.begin(), attacks_.end(), [&](const TowerAttack& attack) {
                return attack.tower == tower.object;
            })) {
            attacks_.push_back({tower.object});
        }
    }
    // Bounded steps preserve the configured cadence instead of dropping shots on slow frames.
    while (delta > 1e-10) {
        const double step = std::min(delta, 1.0 / 60);
        for (std::size_t index = 0; index < instances.size(); ++index) {
            const auto& tower = instances[index];
            auto& attack = *std::find_if(attacks_.begin(), attacks_.end(), [&](const TowerAttack& value) {
                return value.tower == tower.object;
            });
            const auto& definition = towers.definition(tower.type);
            const auto level = towers.progress(tower.object).level;
            const auto& statistics = definition.statistics(level);
            attack.cooldown = std::min(attack.cooldown, 1.0 / statistics.fireRate);
            const Enemy* nearest = nullptr;
            const auto origin = firingOrigin(tower);
            const auto valid = [&](const Enemy& enemy) {
                const auto offset = enemy.position - (tower.position - glm::dvec3{0, tower.halfExtent.y, 0});
                return enemy.active && enemy.alive &&
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
            const bool rescan = attack.target || attack.searchDelay <= 0 ||
                                attack.geometryRevision != geometry->revision() ||
                                attack.enemyCount != enemies.enemies().size();
            if (!nearest && rescan) {
                for (const auto& enemy : enemies.enemies()) {
                    // Range uses terrain-level entity anchors, independent of either model's visual height.
                    const auto offset = enemy.position - (tower.position - glm::dvec3{0, tower.halfExtent.y, 0});
                    const double distance = glm::dot(offset, offset);
                    if (enemy.active && enemy.alive && distance <= nearestDistance &&
                        (!nearest || distance < nearestDistance || enemy.id < nearest->id) && valid(enemy)) {
                        nearest = &enemy;
                        nearestDistance = distance;
                    }
                }
                attack.searchDelay = 0.15;
            }
            attack.geometryRevision = geometry->revision();
            attack.enemyCount = enemies.enemies().size();
            attack.target = nearest ? std::optional{nearest->id} : std::nullopt;
            if (attack.target && attack.cooldown <= 1e-10) {
                projectiles_.spawn(origin, *attack.target, definition, level, tower.object);
                attack.cooldown += 1.0 / statistics.fireRate;
                ++shotsFired_;
            }
            attack.cooldown -= step;
            if (!attack.target) {
                attack.cooldown = std::max(0.0, attack.cooldown);
            }
        }
        projectiles_.update(enemies, step, geometry);
        for (auto& attack : attacks_) {
            if (attack.target && !enemies.find(*attack.target)) {
                attack.target.reset();
            }
        }
        delta -= step;
    }
}

void CombatSystem::clear() noexcept {
    projectiles_.clear();
    attacks_.clear();
    shotsFired_ = 0;
}

} // namespace grimlock::game
