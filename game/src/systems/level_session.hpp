#pragma once

#include "navigation/navigation_session.hpp"
#include "systems/wave_system.hpp"

namespace grimlock::game {

// Binds an authored schedule to the existing scene/navigation/enemy systems.
class LevelSession final {
public:
    void begin(Engine& engine, NavigationSession& navigation, LevelDefinition definition);
    void stop(NavigationSession& navigation);
    void startWave(NavigationSession& navigation);
    void update(Engine& engine, NavigationSession& navigation, double delta);

    [[nodiscard]] const WaveSystem* waves() const noexcept {
        return waves_ ? &*waves_ : nullptr;
    }

private:
    std::optional<WaveSystem> waves_;
    const Scene* scene_ = nullptr;
    std::vector<EnemyTypeId> types_;
    std::uint64_t arrivals_ = 0;
};

} // namespace grimlock::game
