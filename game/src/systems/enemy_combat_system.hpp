#pragma once

#include "systems/enemy_system.hpp"
#include "systems/hero_system.hpp"
#include "systems/melee_navigation.hpp"

namespace grimlock::game {

class EnemyCombatSystem final {
public:
    void defendObjective(MeleeBody body, ObjectId object = 0) {
        objective_ = body;
        objectiveObject_ = object;
    }

    [[nodiscard]] std::uint64_t objectiveHits() const noexcept {
        return objectiveHits_;
    }

    void prepare(EnemySystem& enemies,
                 const TowerSystem& towers,
                 HeroSystem* heroes,
                 glm::dvec2 objective,
                 double delta,
                 const CombatGeometry* geometry = nullptr);
    void resolve(Engine& engine, EnemySystem& enemies, TowerSystem& towers, HeroSystem* heroes, double delta);
    void stop(EnemySystem& enemies);

private:
    const CombatGeometry* geometry_ = nullptr;
    std::optional<MeleeBody> objective_;
    ObjectId objectiveObject_ = 0;
    std::uint64_t objectiveHits_ = 0;
    bool clearStrike(const Enemy& enemy, const MeleeBody& body, ObjectId target = 0) const;

    struct Target {
        DefensiveTarget reference;
        MeleeBody body;
        ObjectId object = 0; // Scene handle used only for geometry, never for target identity.
        Team team = Team::player;
    };

    [[nodiscard]] const Target* find(DefensiveTarget target) const;
    static int priority(const EnemyCombatDefinition& definition, DefensiveTargetKind kind);
    static void release(EnemySystem& enemies, Enemy& enemy, bool rejected);
    std::vector<Target> targets_;
    std::vector<MovementBody> movementBodies_;
    std::vector<EnemyId> arrivals_;
    std::vector<std::pair<double, std::size_t>> candidates_;
    NavigationPath approachPath_;
};

} // namespace grimlock::game
