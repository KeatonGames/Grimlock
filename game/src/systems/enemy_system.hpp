#pragma once

#include "entities/components.hpp"
#include "systems/crowd_movement.hpp"

#include <cmath>
#include <stdexcept>

namespace grimlock::game {

struct EnemyMetrics {
    double updateMilliseconds = 0;
    CrowdMetrics crowd;
    std::uint64_t arrived = 0;
    std::uint64_t defeated = 0;
    std::uint64_t killGold = 0;
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

    void setMovementObstacles(std::span<const MovementBody> bodies) {
        movement_.setObstacles(bodies);
    }

    [[nodiscard]] bool applyDamage(EnemyId id,
                                   float damage,
                                   DefensiveTarget attacker = {},
                                   DamageType damageType = DamageType::physical);
    [[nodiscard]] bool canDamage(const Enemy& enemy, DamageType type) const noexcept;
    void updateEffects(double delta);

    [[nodiscard]] std::size_t deathEffectCount() const noexcept {
        return deaths_.size();
    }

    [[nodiscard]] std::size_t coinEffectCount() const noexcept {
        return coins_.size();
    }

    void setSpeed(double speed, EnemyTypeId type = 0);
    void setSpawnSpeed(double speed, EnemyTypeId type);
    void setSharedDestination(std::optional<glm::dvec2> destination);
    void setDestinationHeight(double height);
    [[nodiscard]] bool setDestination(EnemyId id, std::optional<glm::dvec2> destination);
    [[nodiscard]] const Enemy* find(EnemyId id) const noexcept;

    [[nodiscard]] const TeamComponent* team(EnemyId id) const noexcept {
        return teams_.get(id);
    }

    [[nodiscard]] const ecs::Registry& registry() const noexcept {
        return registry_;
    }

    // Conservative X/Z candidates; callers retain exact range, elevation and visibility tests.
    // Visitors must not mutate the system while walking its packed storage.
    template <class Visitor> void visitNearby(glm::dvec3 position, double range, Visitor visitor) const {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
            !std::isfinite(range) || range < 0) {
            throw std::invalid_argument("Nearby query requires a finite position and nonnegative range.");
        }
        const double binsAcross = range * 0.5 + 2;
        // Broad queries and small populations are cheaper to scan than to visit mostly empty buckets.
        if (enemies_.size() <= 32 || binsAcross * binsAcross >= enemies_.size() || std::abs(position.x) > 1e7 ||
            std::abs(position.z) > 1e7) {
            for (const auto& enemy : enemies_) {
                visitor(enemy);
            }
            return;
        }
        if (neighborsDirty_) {
            queryNeighbors_.build(enemies_, 4, glm::dvec3{0});
            neighborsDirty_ = false;
        }
        queryNeighbors_.visit(position, range, [&](std::size_t index) {
            visitor(enemies_[index]);
        });
    }

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
        std::optional<std::size_t> idleClip, runClip;
    };

    struct DeathEffect {
        InstanceId instance;
        double remaining;
        bool finishAnimation = false;
        double dissolveTime = 0;
    };

    struct CoinEffect {
        InstanceId instance;
        glm::dvec3 origin;
        double age = 0;
    };

    void loadModel(EnemyTypeId type);
    void updateModel(Enemy& enemy);
    void removeAt(std::size_t index) noexcept;

    Engine& engine_;
    std::vector<AnimalType> types_;
    ecs::Registry registry_;
    ecs::Storage<Enemy>& enemies_;
    ecs::Storage<TeamComponent>& teams_;
    mutable EnemyNeighbors queryNeighbors_;
    mutable bool neighborsDirty_ = true;
    std::vector<DeathEffect> deaths_;
    std::vector<CoinEffect> coins_;
    std::optional<ModelId> coinModel_;
    std::uint64_t nextSpawnOrder_ = 1;
    CrowdMovement movement_;
    EnemyNeighbors spawnNeighbors_;
    EnemyMetrics metrics_;
    std::optional<glm::dvec2> sharedDestination_;
};

} // namespace grimlock::game
