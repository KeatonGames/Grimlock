#include "systems/wave_system.hpp"

#include "systems/health.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace grimlock::game {

const char* levelPhaseName(LevelPhase phase) noexcept {
    switch (phase) {
    case LevelPhase::preparation:
        return "Preparation (day)";
    case LevelPhase::battle:
        return "Battle (night)";
    case LevelPhase::victory:
        return "Victory";
    case LevelPhase::defeat:
        return "Defeat";
    }
    return "Unknown";
}

WaveSystem::WaveSystem(LevelDefinition definition) : definition_(std::move(definition)) {
    validateLevelDefinition(definition_);
    restart();
}

void WaveSystem::restart() {
    phase_ = LevelPhase::preparation;
    wave_ = 0;
    health_ = definition_.objectiveHealth;
    elapsed_ = 0;
    progress_.clear();
}

void WaveSystem::startWave() {
    if (phase_ != LevelPhase::preparation) {
        throw std::logic_error("Waves can only start during preparation.");
    }
    progress_.assign(definition_.waves[wave_].groups.size(), {});
    elapsed_ = 0;
    phase_ = LevelPhase::battle;
}

std::size_t WaveSystem::pending() const noexcept {
    if (phase_ != LevelPhase::battle) {
        return 0;
    }
    std::size_t count = 0;
    const auto& groups = definition_.waves[wave_].groups;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        count += groups[i].count - progress_[i].spawned;
    }
    return count;
}

void WaveSystem::update(double delta, std::size_t activeEnemies, std::uint64_t arrivals, const Spawn& spawn) {
    if (!std::isfinite(delta) || delta < 0 || !std::isfinite(elapsed_ + delta)) {
        throw std::invalid_argument("Wave delta must be finite and nonnegative.");
    }
    if (phase_ != LevelPhase::battle) {
        return;
    }
    // Compare before multiplying so very large arrival counts cannot overflow health arithmetic.
    const auto damage = definition_.damagePerArrival;
    const auto lethalCount = health_ / damage + (health_ % damage != 0);
    if (arrivals >= lethalCount) {
        health_ = 0;
        phase_ = LevelPhase::defeat;
        return;
    }
    receiveDamage(health_, arrivals * damage);
    elapsed_ += delta;
    const auto& groups = definition_.waves[wave_].groups;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const auto& group = groups[i];
        auto& progress = progress_[i];
        while (progress.spawned < group.count && elapsed_ >= group.start + progress.batches * group.interval) {
            const auto count = std::min(group.batchSize - progress.inBatch, group.count - progress.spawned);
            const auto spawned = spawn(group, count);
            if (spawned > count) {
                throw std::logic_error("Wave spawn returned more enemies than requested.");
            }
            progress.spawned += spawned;
            progress.inBatch += spawned;
            activeEnemies += spawned;
            if (progress.inBatch == group.batchSize) {
                progress.inBatch = 0;
                ++progress.batches;
            }
            if (spawned < count) {
                break;
            }
        }
    }
    if (!pending() && !activeEnemies) {
        ++wave_;
        phase_ = wave_ == definition_.waves.size() ? LevelPhase::victory : LevelPhase::preparation;
    }
}

} // namespace grimlock::game
