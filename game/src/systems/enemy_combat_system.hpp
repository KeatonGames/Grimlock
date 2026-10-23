#pragma once

#include "systems/enemy_system.hpp"
#include "systems/hero_system.hpp"
#include "systems/melee_navigation.hpp"

namespace grimlock::game {

class EnemyCombatSystem final {
public:
    void prepare(EnemySystem& enemies,
                 const TowerSystem& towers,
                 HeroSystem* heroes,
                 glm::dvec2 objective,
                 double delta);
    void resolve(Engine& engine, EnemySystem& enemies, TowerSystem& towers, HeroSystem* heroes, double delta);
    void stop(EnemySystem& enemies);

private:
    struct Target {
        DefensiveTarget reference;
        MeleeBody body;
    };

    [[nodiscard]] const Target* find(DefensiveTarget target) const;
    static int priority(const EnemyCombatDefinition& definition, DefensiveTargetKind kind);
    static void release(EnemySystem& enemies, Enemy& enemy, bool rejected);
    std::vector<Target> targets_;
    std::vector<EnemyId> arrivals_;
    std::vector<std::pair<double, std::size_t>> candidates_;
    NavigationPath approachPath_;
};

} // namespace grimlock::game
