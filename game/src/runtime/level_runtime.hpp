#pragma once

#include "systems/hero_combat_system.hpp"
#include "systems/hero_system.hpp"
#include "systems/level_session.hpp"

namespace grimlock::game {

// Absolute source levels under assets/levels use their own asset tree instead of a staged copy.
[[nodiscard]] std::filesystem::path assetRootForLevel(const std::filesystem::path& level);

// Player runtime state. Never writes authored level/scene files.
class LevelRuntime final {
public:
    void load(Engine& engine, const std::filesystem::path& level);
    void restart(Engine& engine);
    void update(Engine& engine, double delta);
    void startWave();
    void beginPlacement(const std::string& type);
    void cancelPlacement() noexcept;
    void updatePlacement(Engine& engine, const std::optional<Ray>& ray, bool confirm);
    void selectObject(Engine& engine, const std::optional<Ray>& ray);
    void beginHeroPlacement();
    void updateHeroPlacement(Engine& engine, const std::optional<Ray>& ray, bool confirm);
    [[nodiscard]] bool commandHero(const Ray& ray);
    [[nodiscard]] bool activateHeroAbility(HeroAbility ability);
    void cancelHeroOrder();
    [[nodiscard]] const Hero* selectedHero() const noexcept;

    [[nodiscard]] HeroSystem* heroes() noexcept {
        return heroes_.get();
    }

    [[nodiscard]] bool deployingHero() const noexcept {
        return deployingHero_;
    }

    [[nodiscard]] const std::string& heroError() const noexcept {
        return heroError_;
    }

    [[nodiscard]] const Tower* inspectedTower() const noexcept;

    [[nodiscard]] LevelSession& level() noexcept {
        return level_;
    }

    [[nodiscard]] NavigationSession& navigation() noexcept {
        return navigation_;
    }

    [[nodiscard]] bool placing() const noexcept {
        return placing_;
    }

    [[nodiscard]] const std::optional<Tower>& preview() const noexcept {
        return preview_;
    }

    [[nodiscard]] const std::string& placementError() const noexcept {
        return placementError_;
    }

    [[nodiscard]] const std::string& selectedTower() const noexcept {
        return selectedTower_;
    }

private:
    NavigationSession navigation_;
    LevelSession level_;
    std::unique_ptr<HeroSystem> heroes_;
    HeroCombatSystem heroCombat_;
    Engine* engine_ = nullptr;
    std::optional<HeroId> selectedHero_, deployingId_;
    bool deployingHero_ = false;
    std::string heroError_;
    std::filesystem::path levelPath_;
    std::string selectedTower_ = "basic", placementError_, rejectedError_;
    bool placing_ = false;
    std::optional<Tower> preview_, rejectedTower_;
    std::uint64_t rejectedRevision_ = 0;
    std::optional<ObjectId> inspectedTower_;
};

} // namespace grimlock::game
