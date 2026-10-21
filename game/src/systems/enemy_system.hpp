#pragma once

#include "systems/crowd_movement.hpp"

namespace grimlock::game {

struct EnemyMetrics {
    double updateMilliseconds = 0;
    CrowdMetrics crowd;
    std::uint64_t arrived = 0;
};

// Owns the chicken instances. Engine must outlive this system.
class EnemySystem final {
public:
    static constexpr std::size_t maximumEnemies = 1000;
    explicit EnemySystem(Engine& engine, EnemyDefinition definition = {});
    ~EnemySystem();
    EnemySystem(const EnemySystem&) = delete;
    EnemySystem& operator=(const EnemySystem&) = delete;

    [[nodiscard]] std::size_t spawn(const Grid* walls, SpawnArea area, std::size_t count);
    [[nodiscard]] bool remove(EnemyId id) noexcept;
    void clear() noexcept;
    void update(const Grid* walls, std::optional<glm::dvec2> destination, double delta);
    void setSpeed(double speed);

    [[nodiscard]] const EnemyDefinition& definition() const noexcept {
        return definition_;
    }

    [[nodiscard]] std::span<const Enemy> enemies() const noexcept {
        return enemies_;
    }

    [[nodiscard]] const EnemyMetrics& metrics() const noexcept {
        return metrics_;
    }

private:
    void loadModel();
    void updateModel(Enemy& enemy);
    void removeAt(std::size_t index) noexcept;

    Engine& engine_;
    EnemyDefinition definition_;
    std::vector<Enemy> enemies_;
    EnemyId nextId_ = 1;
    std::optional<ModelId> model_;
    ModelInfo modelInfo_;
    std::optional<std::size_t> movementClip_;
    CrowdMovement movement_;
    EnemyNeighbors spawnNeighbors_;
    EnemyMetrics metrics_;
};

} // namespace grimlock::game
