#pragma once

#include "navigation/navigation_session.hpp"
#include "systems/combat_system.hpp"
#include "systems/economy_system.hpp"
#include "systems/wave_system.hpp"

namespace grimlock::game {

// Binds an authored schedule to the existing scene/navigation/enemy systems.
class LevelSession final {
public:
    void begin(Engine& engine, NavigationSession& navigation, LevelDefinition definition);
    void stop(NavigationSession& navigation);
    void startWave(NavigationSession& navigation);
    void update(Engine& engine, NavigationSession& navigation, double delta);
    [[nodiscard]] std::string constructionError(const NavigationSession& navigation, const Tower& tower) const;
    [[nodiscard]] ObjectId buildTower(Engine& engine, NavigationSession& navigation, const Tower& tower);

    [[nodiscard]] const EconomySystem& economy() const noexcept {
        return economy_;
    }

    [[nodiscard]] std::optional<std::uint64_t> lastWaveReward() const noexcept {
        return lastWaveReward_;
    }

    [[nodiscard]] const WaveSystem* waves() const noexcept {
        return waves_ ? &*waves_ : nullptr;
    }

    [[nodiscard]] const CombatSystem* combat() const noexcept {
        return combat_ ? &*combat_ : nullptr;
    }

private:
    void updateBattle(Engine& engine, NavigationSession& navigation, double delta);
    std::optional<CombatSystem> combat_;
    std::optional<WaveSystem> waves_;
    const Scene* scene_ = nullptr;
    std::vector<EnemyTypeId> types_;
    std::uint64_t arrivals_ = 0;
    EconomySystem economy_;
    std::optional<std::uint64_t> lastWaveReward_;
};

} // namespace grimlock::game
