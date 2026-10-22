#include "runtime/level_runtime.hpp"

#include <stdexcept>

namespace grimlock::game {

std::filesystem::path assetRootForLevel(const std::filesystem::path& level) {
    return level.is_absolute() && level.parent_path().filename() == "levels" ? level.parent_path().parent_path()
                                                                             : std::filesystem::path{"assets"};
}

void LevelRuntime::load(Engine& engine, const std::filesystem::path& level) {
    const auto path = level.is_absolute() ? level : engine.assetRoot().parent_path() / level;
    auto definition = loadLevelDefinition(path);
    auto scene = engine.loadScene(engine.assetRoot() / definition.scene);
    cancelPlacement();
    level_.stop(navigation_);
    navigation_.shutdown();
    engine.setScene(std::move(scene));
    level_.begin(engine, navigation_, std::move(definition));
    levelPath_ = path;
    selectedTower_ = "basic";
    engine.setObjectSelectionEnabled(false);
    engine.setCameraNavigationEnabled();
}

void LevelRuntime::restart(Engine& engine) {
    if (levelPath_.empty()) {
        throw std::logic_error("Load a level before restarting.");
    }
    load(engine, levelPath_);
}

void LevelRuntime::update(Engine& engine, double delta) {
    level_.update(engine, navigation_, delta);
    if (!level_.waves() || level_.waves()->phase() != LevelPhase::preparation) {
        cancelPlacement();
    }
}

void LevelRuntime::startWave() {
    level_.startWave(navigation_);
    cancelPlacement();
}

void LevelRuntime::beginPlacement(const std::string& type) {
    const auto tower = navigation_.towers().definition(type).instance();
    if (const auto error = level_.constructionError(navigation_, tower); !error.empty()) {
        throw std::invalid_argument(error);
    }
    cancelPlacement();
    selectedTower_ = type;
    placing_ = true;
}

void LevelRuntime::cancelPlacement() noexcept {
    placing_ = false;
    preview_.reset();
    rejectedTower_.reset();
    placementError_.clear();
    rejectedError_.clear();
    navigation_.cancelPlacement();
}

void LevelRuntime::updatePlacement(Engine& engine, const std::optional<Ray>& ray, bool confirm) {
    preview_.reset();
    if (!placing_) {
        return;
    }
    if (!level_.waves() || level_.waves()->phase() != LevelPhase::preparation) {
        cancelPlacement();
        return;
    }
    navigation_.synchronize(engine);
    preview_ = ray ? navigation_.towerAt(*ray, selectedTower_) : std::nullopt;
    if (!preview_) {
        placementError_ = "Move over the terrain to build.";
        return;
    }
    if (rejectedTower_ != preview_ || rejectedRevision_ != navigation_.revision()) {
        rejectedError_.clear();
    }
    placementError_ = level_.constructionError(navigation_, *preview_);
    if (placementError_.empty()) {
        placementError_ = navigation_.checkTower(engine, *preview_, false);
    }
    if (confirm && placementError_.empty()) {
        try {
            (void)level_.buildTower(engine, navigation_, *preview_);
            rejectedError_.clear();
            placementError_ = navigation_.checkTower(engine, *preview_, false);
        } catch (const std::exception& error) {
            rejectedTower_ = preview_;
            rejectedRevision_ = navigation_.revision();
            rejectedError_ = error.what();
        }
    }
    if (placementError_.empty()) {
        placementError_ = rejectedError_;
    }
}

} // namespace grimlock::game
