#pragma once

#include "entities/projectile.hpp"
#include "entities/tower_definition.hpp"
#include "systems/enemy_system.hpp"

namespace grimlock::game {

// Runtime instances share one cube model and never become serialized scene objects.
class ProjectileSystem final {
public:
    explicit ProjectileSystem(Engine& engine) : engine_(engine) {
    }

    ~ProjectileSystem();
    ProjectileSystem(const ProjectileSystem&) = delete;
    ProjectileSystem& operator=(const ProjectileSystem&) = delete;
    void spawn(glm::dvec3 origin, EnemyId target, const TowerDefinition& definition, std::size_t level = 1);
    void update(EnemySystem& enemies, double delta);
    void clear() noexcept;

    [[nodiscard]] std::span<const Projectile> projectiles() const noexcept {
        return projectiles_;
    }

    [[nodiscard]] std::uint64_t impacts() const noexcept {
        return impacts_;
    }

private:
    void removeAt(std::size_t index) noexcept;
    Engine& engine_;
    std::optional<ModelId> model_;
    std::vector<Projectile> projectiles_;
    std::uint64_t impacts_ = 0;
};

} // namespace grimlock::game
