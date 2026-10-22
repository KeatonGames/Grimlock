#pragma once

#include "entities/level_definition.hpp"

#include <functional>

namespace grimlock::game {

enum class LevelPhase {
    preparation,
    battle,
    victory,
    defeat
};
[[nodiscard]] const char* levelPhaseName(LevelPhase phase) noexcept;

// Owns a fixed authored schedule. Spawn callbacks report actual successes; occupied slots retry without dropping
// counts.
class WaveSystem final {
public:
    using Spawn = std::function<std::size_t(const WaveGroup&, std::size_t)>;
    explicit WaveSystem(LevelDefinition definition);
    void restart();
    void startWave();
    void update(double delta, std::size_t activeEnemies, std::uint64_t arrivals, const Spawn& spawn);

    [[nodiscard]] const LevelDefinition& definition() const noexcept {
        return definition_;
    }

    [[nodiscard]] LevelPhase phase() const noexcept {
        return phase_;
    }

    [[nodiscard]] std::size_t wave() const noexcept {
        return wave_;
    }

    [[nodiscard]] std::uint64_t health() const noexcept {
        return health_;
    }

    [[nodiscard]] std::size_t pending() const noexcept;

    [[nodiscard]] double elapsed() const noexcept {
        return elapsed_;
    }

private:
    struct GroupProgress {
        std::size_t spawned = 0, inBatch = 0, batches = 0;
    };

    LevelDefinition definition_;
    std::vector<GroupProgress> progress_;
    LevelPhase phase_ = LevelPhase::preparation;
    std::size_t wave_ = 0;
    std::uint64_t health_ = 0;
    double elapsed_ = 0;
};

} // namespace grimlock::game
