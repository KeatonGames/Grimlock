#pragma once

#include "systems/enemy_system.hpp"
#include "systems/hero_system.hpp"

namespace grimlock::game {

enum class HeroAbility {
    cleave
};

// Combat decisions own no render resources. All damage goes through EnemySystem.
class HeroCombatSystem final {
public:
    void update(HeroSystem& heroes, EnemySystem& enemies, double delta, bool battle);
    [[nodiscard]] bool attack(HeroSystem& heroes, const EnemySystem& enemies, HeroId hero, EnemyId target);
    [[nodiscard]] bool activate(HeroSystem& heroes, HeroId hero, HeroAbility ability, bool battle);
    [[nodiscard]] static std::optional<EnemyId> pick(Engine& engine, const EnemySystem& enemies, const Ray& ray);

private:
    void step(HeroSystem& heroes, EnemySystem& enemies, Hero& hero, double delta);
    void impact(HeroSystem& heroes, EnemySystem& enemies, Hero& hero);
    void acquire(HeroSystem& heroes, const EnemySystem& enemies, Hero& hero);
    void returnToDefense(HeroSystem& heroes, Hero& hero);
    static void stop(Hero& hero);
    static void abandon(HeroSystem& heroes, Hero& hero);
    static bool inRange(const HeroSystem& heroes, const Hero& hero, const Enemy& enemy, double range);
    std::vector<EnemyId> victims_;
    std::vector<std::pair<double, EnemyId>> candidates_;
    NavigationPath approachPath_;
};

} // namespace grimlock::game
