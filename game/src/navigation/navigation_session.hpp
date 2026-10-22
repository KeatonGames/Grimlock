#pragma once

#include "systems/enemy_system.hpp"
#include "systems/placement_system.hpp"
#include "systems/tower_system.hpp"

namespace grimlock::game {

struct SpawnRoute {
    EnemyTypeId type;
    SpawnArea area;
    glm::dvec2 destination;
};

// Owns the runtime enemy preview and its world-space X/Z destination.
class NavigationSession final {
public:
    ~NavigationSession() {
        shutdown();
    }

    void synchronize(Engine& engine);
    void update(Engine& engine, double delta);
    [[nodiscard]] std::size_t spawn(Engine& engine, std::size_t count = 100);
    [[nodiscard]] std::size_t spawnAt(Engine& engine, EnemyTypeId type, SpawnArea area, std::size_t count);
    [[nodiscard]] const NavMesh& surface(Engine& engine, EnemyTypeId type);
    void setSpawnRoutes(std::vector<SpawnRoute> routes);
    void reset(Engine& engine);
    void shutdown() noexcept;
    void rebuild(Engine& engine);

    void setDestination(std::optional<glm::dvec2> destination);
    void selectAnimal(Engine& engine, EnemyTypeId type);
    [[nodiscard]] std::optional<Tower> towerAt(const Ray& ray, const std::string& type = "basic", double yaw = 0) const;

    void setPlacementEnabled(bool enabled) noexcept {
        placementEnabled_ = enabled;
    }

    [[nodiscard]] bool placementEnabled() const noexcept {
        return placementEnabled_;
    }

    [[nodiscard]] const PlacementSystem& placement() const noexcept {
        return placement_;
    }

    [[nodiscard]] std::string checkTower(Engine& engine,
                                         const Tower& tower,
                                         bool checkRoutes = true,
                                         PlacementMode mode = PlacementMode::gameplay);
    [[nodiscard]] ObjectId placeTower(Engine& engine, const Tower& tower, PlacementMode mode = PlacementMode::gameplay);
    [[nodiscard]] bool removeTower(Engine& engine, ObjectId object);

    void cancelPlacement() noexcept {
        candidate_.reset();
    }

    [[nodiscard]] const TowerSystem& towers() const noexcept {
        return towers_;
    }

    [[nodiscard]] std::uint64_t revision() const noexcept {
        return navigationRevision_;
    }

    [[nodiscard]] EnemyTypeId selectedAnimal() const noexcept {
        return selectedType_;
    }

    [[nodiscard]] std::optional<glm::dvec2> destination() const noexcept {
        return destination_;
    }

    [[nodiscard]] EnemySystem* enemies() noexcept {
        return enemies_.get();
    }

    [[nodiscard]] SpawnArea& spawnArea() noexcept {
        return spawnArea_;
    }

    [[nodiscard]] const NavMesh& navigation() const noexcept {
        return typeProfiles_.empty() || typeProfiles_[selectedType_] == 0
                   ? navigation_
                   : *profiles_[typeProfiles_[selectedType_] - 1].mesh;
    }

private:
    Engine* engine_ = nullptr;
    const Scene* scene_ = nullptr;
    const Grid* grid_ = nullptr;
    std::optional<GridConfiguration> configuration_;
    std::optional<glm::dvec2> destination_;
    std::unique_ptr<EnemySystem> enemies_;
    SpawnArea spawnArea_;
    NavMesh navigation_;

    struct NavigationProfile {
        EnemyTypeId type;
        std::unique_ptr<NavMesh> mesh;
    };

    std::vector<NavigationProfile> profiles_;
    std::vector<std::size_t> typeProfiles_;
    EnemyTypeId selectedType_ = 0;
    NavigationGeometry geometry_;
    std::vector<SpawnRoute> spawnRoutes_;
    std::optional<std::uint64_t> geometryRevision_;
    TowerSystem towers_;
    PlacementSystem placement_;
    bool placementEnabled_ = true;
    std::vector<TerrainBox> terrain_;
    std::uint64_t towerRevision_ = 0, navigationRevision_ = 0;

    struct PreparedProfile {
        EnemyTypeId type;
        std::unique_ptr<NavMesh> mesh;
        bool hadSpawnRoute = false, hasSpawnRoute = false;
    };

    struct PlacementCandidate {
        Tower tower;
        SpawnArea area;
        glm::dvec2 destination;
        EnemyTypeId type;
        std::uint64_t revision;
        NavigationGeometry geometry;
        std::vector<PreparedProfile> profiles;
        std::string error;
    };

    std::optional<PlacementCandidate> candidate_;
    void preparePlacement(const Tower& tower);
    [[nodiscard]] bool profileInUse(EnemyTypeId type) const;
    [[nodiscard]] std::string checkEnemyRoutes(const PlacementCandidate& candidate) const;
    [[nodiscard]] NavMesh& navigationFor(EnemyTypeId type);
    void buildProfile(EnemyTypeId type);
    void buildNavigation();
};

} // namespace grimlock::game
