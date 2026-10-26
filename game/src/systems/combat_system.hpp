#pragma once

#include "entities/tower_attack.hpp"
#include "systems/projectile_system.hpp"
#include "systems/tower_system.hpp"

namespace grimlock::game {

class CombatSystem final {
public:
    explicit CombatSystem(Engine& engine) : engine_(engine), projectiles_(engine) {
    }

    void update(const TowerSystem& towers,
                EnemySystem& enemies,
                double delta,
                const CombatGeometry* geometry = nullptr);
    void clear() noexcept;

    void clearProjectiles() noexcept {
        projectiles_.clear();
    }

    [[nodiscard]] const ProjectileSystem& projectiles() const noexcept {
        return projectiles_;
    }

    [[nodiscard]] std::span<const TowerAttack> attacks() const noexcept {
        return attacks_;
    }

    [[nodiscard]] std::uint64_t shotsFired() const noexcept {
        return shotsFired_;
    }

private:
    Engine& engine_;
    CombatGeometry geometry_;
    ProjectileSystem projectiles_;
    std::vector<TowerAttack> attacks_;
    std::uint64_t shotsFired_ = 0;
};

} // namespace grimlock::game
