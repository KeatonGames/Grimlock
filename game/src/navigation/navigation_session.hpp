#pragma once

#include "systems/enemy_system.hpp"

namespace grimlock::game {

// Owns the runtime enemy preview and its world-space X/Z destination.
class NavigationSession final {
public:
    void synchronize(Engine& engine);
    void update(Engine& engine, double delta);
    [[nodiscard]] std::size_t spawn(Engine& engine, std::size_t count = 100);
    void reset(Engine& engine);
    void shutdown() noexcept;

    void setDestination(std::optional<glm::dvec2> destination);

    [[nodiscard]] std::optional<glm::dvec2> destination() const noexcept {
        return destination_;
    }

    [[nodiscard]] EnemySystem* enemies() noexcept {
        return enemies_.get();
    }

    [[nodiscard]] SpawnArea& spawnArea() noexcept {
        return spawnArea_;
    }

private:
    Engine* engine_ = nullptr;
    const Scene* scene_ = nullptr;
    const Grid* grid_ = nullptr;
    std::optional<GridConfiguration> configuration_;
    std::optional<glm::dvec2> destination_;
    std::unique_ptr<EnemySystem> enemies_;
    SpawnArea spawnArea_;
};

} // namespace grimlock::game
