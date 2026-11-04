#pragma once

#include "entities/hero.hpp"
#include "navigation/level_geometry.hpp"
#include "systems/combat_geometry.hpp"
#include "systems/projectile_system.hpp"
#include "systems/tower_system.hpp"

namespace grimlock::game {

// Owns runtime hero objects and a radius-aware Recast profile. Engine must outlive this system.
class HeroSystem final {
public:
    explicit HeroSystem(Engine& engine);
    ~HeroSystem();
    HeroSystem(const HeroSystem&) = delete;
    HeroSystem& operator=(const HeroSystem&) = delete;
    void synchronize(const TowerSystem& towers);

    void shareCombatGeometry(const CombatGeometry& geometry) noexcept {
        sharedGeometry_ = &geometry;
    }

    [[nodiscard]] const CombatGeometry& combatGeometry() const noexcept {
        return sharedGeometry_ ? *sharedGeometry_ : combatGeometry_;
    }

    [[nodiscard]] HeroId deploy(glm::dvec3 position, std::optional<HeroId> existing = {}, std::size_t type = 0);
    [[nodiscard]] bool command(HeroId hero, glm::dvec3 destination);
    [[nodiscard]] bool cancelCommand(HeroId hero);
    [[nodiscard]] bool applyDamage(HeroId hero, float damage);
    void update(double delta, bool movingAllowed, std::span<const Enemy> enemies = {});
    void clear() noexcept;
    [[nodiscard]] bool remove(HeroId hero) noexcept;

    [[nodiscard]] const ecs::Registry& registry() const noexcept {
        return registry_;
    }

    void preview(std::optional<glm::dvec3> position, bool valid = false, std::size_t type = 0);
    [[nodiscard]] bool validPosition(glm::dvec3 position,
                                     std::optional<HeroId> ignore = {},
                                     std::size_t type = 0) const;
    [[nodiscard]] std::optional<glm::dvec3> ground(const Ray& ray) const;
    [[nodiscard]] const Hero* find(HeroId id) const noexcept;

    [[nodiscard]] const TeamComponent* team(HeroId id) const noexcept {
        return teams_.get(id);
    }

    [[nodiscard]] std::optional<HeroId> fromObject(ObjectId object) const noexcept;

    [[nodiscard]] std::span<const Hero> heroes() const noexcept {
        return heroes_;
    }

    [[nodiscard]] const HeroDefinition& definition(std::size_t type = 0) const {
        return types_.at(type)->definition;
    }

    [[nodiscard]] const HeroDefinition& definition(const Hero& hero) const {
        return definition(hero.type);
    }

    [[nodiscard]] std::size_t typeCount() const noexcept {
        return types_.size();
    }

    [[nodiscard]] const NavMesh& navigation(std::size_t type = 0) const {
        return types_.at(type)->navigation;
    }

    [[nodiscard]] std::string towerError(const Tower& tower) const;

private:
    friend class HeroCombatSystem;
    bool navigate(Hero& hero, glm::dvec3 destination);
    void playAttack(Hero& hero);
    void finishAttack(Hero& hero);
    Hero* findMutable(HeroId id) noexcept;
    void updateModel(Hero& hero);
    void step(Hero& hero, double delta, std::span<const Enemy> enemies);
    glm::dvec3 steerAroundEnemies(Hero& hero, glm::dvec3 target, std::span<const Enemy> enemies) const;
    [[nodiscard]] Transform transform(glm::dvec3 position, double yaw, std::size_t type) const;
    Engine& engine_;
    const Scene* scene_;

    struct TypeResources {
        HeroDefinition definition;
        ModelId model;
        ModelInfo modelInfo;
        std::optional<std::size_t> idleClip, movementClip, attackClip, deathClip;
        NavMesh navigation;
    };

    std::vector<std::unique_ptr<TypeResources>> types_;
    ProjectileSystem projectiles_;
    ecs::Registry registry_;
    ecs::Storage<Hero>& heroes_;
    ecs::Storage<TeamComponent>& teams_;
    std::uint64_t nextSpawnOrder_ = 1;
    LevelGeometry levelGeometry_;
    CombatGeometry combatGeometry_;
    const CombatGeometry* sharedGeometry_ = nullptr;
    std::uint64_t geometryRevision_ = 0, towerRevision_ = 0;
    std::optional<InstanceId> preview_;
    std::size_t previewType_ = 0;
};

} // namespace grimlock::game
