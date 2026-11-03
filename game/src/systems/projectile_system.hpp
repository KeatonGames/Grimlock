#pragma once

#include "entities/projectile.hpp"
#include "entities/tower_definition.hpp"
#include "systems/combat_geometry.hpp"
#include "systems/enemy_system.hpp"

namespace grimlock::game {

// Runtime instances share one cube model and never become serialized scene objects.
class ProjectileSystem final {
public:
    explicit ProjectileSystem(Engine& engine)
        : engine_(engine), projectiles_(registry_.storage<Projectile>()),
          lifetimes_(registry_.storage<ProjectileLifetimeComponent>()) {
        registry_.reserveEntities(64);
        registry_.reserve<Projectile>(64);
        registry_.reserve<ProjectileLifetimeComponent>(64);
    }

    ~ProjectileSystem();
    ProjectileSystem(const ProjectileSystem&) = delete;
    ProjectileSystem& operator=(const ProjectileSystem&) = delete;
    void spawn(glm::dvec3 origin,
               EnemyId target,
               const TowerDefinition& definition,
               std::size_t level = 1,
               ObjectId source = 0,
               double lifetime = std::numeric_limits<double>::infinity());
    void update(EnemySystem& enemies, double delta, const CombatGeometry* geometry = nullptr);
    void clear() noexcept;

    [[nodiscard]] std::span<const Projectile> projectiles() const noexcept {
        return projectiles_;
    }

    [[nodiscard]] const ecs::Registry& registry() const noexcept {
        return registry_;
    }

    [[nodiscard]] std::uint64_t impacts() const noexcept {
        return impacts_;
    }

private:
    void removeAt(std::size_t index) noexcept;
    Engine& engine_;
    std::optional<ModelId> model_;
    ecs::Registry registry_;
    ecs::Storage<Projectile>& projectiles_;
    ecs::Storage<ProjectileLifetimeComponent>& lifetimes_;
    std::uint64_t impacts_ = 0;
    CombatGeometry geometry_;
};

} // namespace grimlock::game
