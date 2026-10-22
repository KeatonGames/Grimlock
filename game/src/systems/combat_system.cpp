#include "systems/combat_system.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace grimlock::game {

void CombatSystem::update(const TowerSystem& towers, EnemySystem& enemies, double delta) {
    if (!std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Combat delta must be finite and nonnegative.");
    }
    const auto instances = towers.towers();
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
            const Enemy* nearest = nullptr;
            double nearestDistance = definition.attackRange * definition.attackRange;
            for (const auto& enemy : enemies.enemies()) {
                const glm::dvec2 offset{enemy.position.x - tower.position.x, enemy.position.z - tower.position.z};
                const double distance = glm::dot(offset, offset);
                if (enemy.active && enemy.alive && distance <= nearestDistance &&
                    (!nearest || distance < nearestDistance || enemy.id < nearest->id)) {
                    nearest = &enemy;
                    nearestDistance = distance;
                }
            }
            attack.target = nearest ? std::optional{nearest->id} : std::nullopt;
            if (attack.target && attack.cooldown <= 1e-10) {
                const auto origin = tower.position + glm::dvec3{0, tower.halfExtent.y + 0.1, 0};
                projectiles_.spawn(origin, *attack.target, definition);
                attack.cooldown += 1.0 / definition.fireRate;
                ++shotsFired_;
            }
            attack.cooldown -= step;
            if (!attack.target) {
                attack.cooldown = std::max(0.0, attack.cooldown);
            }
        }
        projectiles_.update(enemies, step);
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
