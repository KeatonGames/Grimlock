#pragma once

#include "systems/crowd_movement.hpp"

namespace grimlock::game {

struct EnemyMetrics {
    double updateMilliseconds = 0;
    CrowdMetrics crowd;
    std::uint64_t arrived = 0;
    std::uint64_t defeated = 0;
};

// Owns mixed animal instances and shared type configurations. Engine must outlive this system.
class EnemySystem final {
public:
    static constexpr std::size_t maximumEnemies = 1000;
    explicit EnemySystem(Engine& engine, EnemyDefinition definition = {});
    ~EnemySystem();
    EnemySystem(const EnemySystem&) = delete;
    EnemySystem& operator=(const EnemySystem&) = delete;

    [[nodiscard]] EnemyTypeId addDefinition(EnemyDefinition definition);
    [[nodiscard]] std::size_t spawn(const NavMesh& navigation, SpawnArea area, std::size_t count, EnemyTypeId type = 0);
    [[nodiscard]] bool remove(EnemyId id) noexcept;
    void clear() noexcept;
    void update(const NavMesh& navigation, std::optional<glm::dvec2> destination, double delta);
    [[nodiscard]] bool applyDamage(EnemyId id, float damage);
    void updateEffects(double delta);

    [[nodiscard]] std::size_t deathEffectCount() const noexcept {
        return deaths_.size();
    }

    void setSpeed(double speed, EnemyTypeId type = 0);
    void setSpawnSpeed(double speed, EnemyTypeId type);
    void setSharedDestination(std::optional<glm::dvec2> destination);
    [[nodiscard]] bool setDestination(EnemyId id, std::optional<glm::dvec2> destination);
    [[nodiscard]] const Enemy* find(EnemyId id) const noexcept;

    [[nodiscard]] std::optional<glm::dvec2> sharedDestination() const noexcept {
        return sharedDestination_;
    }

    [[nodiscard]] const EnemyDefinition& definition(EnemyTypeId type = 0) const {
        return types_.at(type).definition;
    }

    [[nodiscard]] std::size_t typeCount() const noexcept {
        return types_.size();
    }

    [[nodiscard]] std::span<const Enemy> enemies() const noexcept {
        return enemies_;
    }

    [[nodiscard]] const EnemyMetrics& metrics() const noexcept {
        return metrics_;
    }

private:
    friend class EnemyCombatSystem;
    void attackAnimation(Enemy& enemy, bool attacking);

    struct AnimalType {
        EnemyDefinition definition;
        std::optional<ModelId> model;
        ModelInfo info;
        std::optional<std::size_t> movementClip;
        std::optional<std::size_t> deathClip;
        std::optional<std::size_t> attackClip;
    };

    struct DeathEffect {
        InstanceId instance;
        double remaining;
    };

    void loadModel(EnemyTypeId type);
    void updateModel(Enemy& enemy);
    void removeAt(std::size_t index) noexcept;

    Engine& engine_;
    std::vector<AnimalType> types_;
    std::vector<Enemy> enemies_;
    std::vector<DeathEffect> deaths_;
    EnemyId nextId_ = 1;
    CrowdMovement movement_;
    EnemyNeighbors spawnNeighbors_;
    EnemyMetrics metrics_;
    std::optional<glm::dvec2> sharedDestination_;
};

} // namespace grimlock::game
