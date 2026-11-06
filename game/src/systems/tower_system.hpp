#pragma once

#include "entities/enemy.hpp"
#include "entities/tower.hpp"
#include "entities/tower_attack.hpp"
#include "entities/tower_definition.hpp"
#include "systems/economy_system.hpp"

#include <grimlock/ray.hpp>
#include <map>
#include <string_view>

namespace grimlock::game {

class LevelGeometry;

class TowerSystem final {
public:
    TowerSystem()
        : towers_(registry_.storage<Tower>()), progress_(registry_.storage<TowerProgress>()),
          attacks_(registry_.storage<TowerAttack>()), teams_(registry_.storage<TeamComponent>()) {
    }

    [[nodiscard]] const ecs::Registry& registry() const noexcept {
        return registry_;
    }

    [[nodiscard]] const TeamComponent* team(ecs::Entity entity) const noexcept {
        return teams_.get(entity);
    }

    [[nodiscard]] ecs::Entity entityForObject(ObjectId object) const noexcept;

    [[nodiscard]] const Tower* find(ecs::Entity entity) const noexcept {
        return towers_.get(entity);
    }

    [[nodiscard]] std::span<const TowerAttack> attacks() const noexcept {
        return attacks_;
    }

    [[nodiscard]] std::optional<ObjectId> pick(const Ray& ray) const;
    void synchronize(Engine& engine);
    void clear() noexcept;
    [[nodiscard]] ObjectId create(Engine& engine, const Tower& tower, ObjectId constructionPoint = 0);
    [[nodiscard]] bool remove(Engine& engine, ObjectId object);
    [[nodiscard]] bool contains(ObjectId object) const noexcept;
    [[nodiscard]] const TowerProgress& progress(ObjectId object) const;
    void upgrade(ObjectId object, EconomySystem& economy);
    void resetUpgrades() noexcept;
    [[nodiscard]] bool applyDamage(Engine& engine, ObjectId tower, float damage);
    void updateEffects(Engine& engine, double delta);

    [[nodiscard]] std::span<const TowerDefinition> definitions() const noexcept {
        return definitions_;
    }

    [[nodiscard]] const TowerDefinition& definition(std::string_view type) const;
    [[nodiscard]] std::string check(const Tower& tower,
                                    std::span<const TerrainBox> terrain,
                                    std::span<const Enemy> enemies,
                                    const LevelGeometry* geometry = nullptr) const;

    [[nodiscard]] std::span<const Tower> towers() const noexcept {
        return towers_;
    }

    [[nodiscard]] std::uint64_t revision() const noexcept {
        return revision_;
    }

    [[nodiscard]] static std::optional<glm::dvec3> terrainHit(const Ray& ray, std::span<const TerrainBox> terrain);
    [[nodiscard]] static bool routeFromArea(const NavMesh& navigation, const SpawnArea& area, glm::dvec3 destination);

private:
    friend class CombatSystem;

    TowerAttack& attack(ObjectId object) {
        return *attacks_.get(entityForObject(object));
    }

    void resetCombat() noexcept;
    const Scene* scene_ = nullptr;
    ecs::Registry registry_;
    ecs::Storage<Tower>& towers_;
    ecs::Storage<TowerProgress>& progress_;
    ecs::Storage<TowerAttack>& attacks_;
    ecs::Storage<TeamComponent>& teams_;
    std::map<ObjectId, ecs::Entity> objectEntities_;
    std::vector<Tower> scratchTowers_;
    std::vector<TerrainBox> props_;
    std::map<std::filesystem::path, ModelInfo> models_;
    std::uint64_t revision_ = 0;
    std::vector<TowerDefinition> definitions_{TowerDefinition{}};
    bool catalogLoaded_ = false;
};

} // namespace grimlock::game
