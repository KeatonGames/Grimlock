#pragma once

#include "systems/enemy_system.hpp"

namespace grimlock::game {

// Owns the runtime enemy preview and its world-space X/Z destination.
class NavigationSession final {
public:
    ~NavigationSession() {
        shutdown();
    }

    void synchronize(Engine& engine);
    void update(Engine& engine, double delta);
    [[nodiscard]] std::size_t spawn(Engine& engine, std::size_t count = 100);
    void reset(Engine& engine);
    void shutdown() noexcept;
    void rebuild(Engine& engine);

    void setDestination(std::optional<glm::dvec2> destination);
    void selectAnimal(Engine& engine, EnemyTypeId type);

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
    std::optional<std::uint64_t> geometryRevision_;
    [[nodiscard]] NavMesh& navigationFor(EnemyTypeId type);
    void buildProfile(EnemyTypeId type);
    void buildNavigation();
};

} // namespace grimlock::game
