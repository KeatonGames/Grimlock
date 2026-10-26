#pragma once

#include "entities/enemy.hpp"
#include "entities/tower.hpp"
#include "entities/tower_definition.hpp"
#include "systems/economy_system.hpp"

#include <grimlock/ray.hpp>
#include <map>

namespace grimlock::game {

class LevelGeometry;

class TowerSystem final {
public:
    [[nodiscard]] std::optional<ObjectId> pick(const Ray& ray) const;
    void synchronize(Engine& engine);
    void clear() noexcept;
    [[nodiscard]] ObjectId create(Engine& engine, const Tower& tower);
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

    [[nodiscard]] const TowerDefinition& definition(const std::string& type) const;
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
    const Scene* scene_ = nullptr;
    std::vector<Tower> towers_;
    std::vector<Tower> scratchTowers_;
    std::vector<TerrainBox> props_;
    std::map<std::filesystem::path, ModelInfo> models_;
    std::uint64_t revision_ = 0;
    std::vector<TowerDefinition> definitions_{TowerDefinition{}};
    std::map<ObjectId, TowerProgress> progress_;
    bool catalogLoaded_ = false;
};

} // namespace grimlock::game
