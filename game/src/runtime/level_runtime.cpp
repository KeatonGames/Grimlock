#include "runtime/level_runtime.hpp"

#include "input/ground_command.hpp"

#include <algorithm>
#include <cmath>
#include <grimlock/ray.hpp>
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
    heroes_.reset();
    selectedHero_.reset();
    level_.stop(navigation_);
    heroError_.clear();
    inspectedTower_.reset();
    navigation_.shutdown();
    engine.setScene(std::move(scene));
    level_.begin(engine, navigation_, std::move(definition));
    engine_ = &engine;
    heroes_ = std::make_unique<HeroSystem>(engine);
    heroes_->synchronize(navigation_.towers());
    for (const auto& position : level_.waves()->definition().heroSpawns) {
        (void)heroes_->deploy(position);
    }
    levelPath_ = path;
    selectedTower_ = "basic";
    engine.setObjectSelectionEnabled(false);
    engine.setCameraNavigationEnabled();
    auto camera = engine.cameraNavigationConfig();
    camera.panDragThreshold = GroundCommandGesture::dragThreshold;
    engine.configureCameraNavigation(camera);
}

void LevelRuntime::restart(Engine& engine) {
    if (levelPath_.empty()) {
        throw std::logic_error("Load a level before restarting.");
    }
    load(engine, levelPath_);
}

void LevelRuntime::update(Engine& engine, double delta) {
    if (!std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Level delta must be finite and nonnegative.");
    }
    do {
        const double step = std::min(delta, 1.0 / 30);
        level_.update(engine, navigation_, step, heroes_.get());
        if (heroes_) {
            heroes_->synchronize(navigation_.towers());
            heroCombat_.update(*heroes_,
                               *navigation_.enemies(),
                               step,
                               level_.waves() && level_.waves()->phase() == LevelPhase::battle);
        }
        delta -= step;
    } while (delta > 1e-10);
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
    inspectedTower_.reset();
    selectedHero_.reset();
    selectedTower_ = type;
    placing_ = true;
}

void LevelRuntime::selectObject(Engine& engine, const std::optional<Ray>& ray) {
    heroError_.clear();
    inspectedTower_.reset();
    selectedHero_.reset();
    const auto object = ray ? engine.pickObject(*ray) : std::nullopt;
    if (object && heroes_) {
        selectedHero_ = heroes_->fromObject(*object);
    }
    if (object && navigation_.towers().contains(*object)) {
        inspectedTower_ = *object;
    }
    if (selectedHero_ || inspectedTower_) {
        engine.scene()->selectObject(object);
    } else {
        engine.clearSelection();
    }
}

const Hero* LevelRuntime::selectedHero() const noexcept {
    return heroes_ && selectedHero_ ? heroes_->find(*selectedHero_) : nullptr;
}

void LevelRuntime::beginHeroPlacement() {
    if (!heroes_ || !level_.waves() || level_.waves()->phase() != LevelPhase::preparation) {
        throw std::invalid_argument("Deploy heroes during preparation.");
    }
    const auto previous = selectedHero_;
    if (const auto* hero = selectedHero(); hero && hero->health <= 0) {
        heroError_ = "A dead hero cannot be redeployed. Restart the level to restore heroes.";
        return;
    }
    cancelPlacement();
    inspectedTower_.reset();
    deployingId_ = previous;
    if (!deployingId_ && !heroes_->heroes().empty()) {
        for (const auto& hero : heroes_->heroes()) {
            if (hero.health > 0) {
                deployingId_ = hero.id;
                break;
            }
        }
        if (!deployingId_) {
            heroError_ = "No living heroes remain. Restart the level to restore heroes.";
            return;
        }
    }
    deployingHero_ = true;
    heroError_.clear();
}

void LevelRuntime::updateHeroPlacement(Engine& engine, const std::optional<Ray>& ray, bool confirm) {
    if (!deployingHero_ || !heroes_) {
        return;
    }
    heroes_->synchronize(navigation_.towers());
    const auto point = ray ? heroes_->ground(*ray) : std::nullopt;
    const bool valid = point && heroes_->validPosition(*point, deployingId_);
    heroError_ = valid ? "" : "Deploy on clear navigable ground.";
    heroes_->preview(point, valid);
    if (valid && confirm) {
        selectedHero_ = heroes_->deploy(*point, deployingId_);
        engine.scene()->selectObject(heroes_->find(*selectedHero_)->object);
        deployingHero_ = false;
        heroes_->preview({});
    }
}

bool LevelRuntime::commandHero(const Ray& ray) {
    if (!selectedHero() || !level_.waves() || level_.waves()->phase() != LevelPhase::battle) {
        return false;
    }
    if (engine_ && navigation_.enemies()) {
        auto visibleRay = ray;
        if (const auto ground = heroes_->ground(ray)) {
            visibleRay.maximum = std::min(ray.maximum, glm::length(*ground - ray.origin));
        }
        if (const auto target = HeroCombatSystem::pick(*engine_, *navigation_.enemies(), visibleRay)) {
            const bool accepted = heroCombat_.attack(*heroes_, *navigation_.enemies(), *selectedHero_, *target);
            heroError_ = accepted ? "" : "Enemy is outside the hero's pursuit range.";
            return accepted;
        }
    }
    const auto point = heroes_->ground(ray);
    const bool accepted = point && heroes_->command(*selectedHero_, *point);
    heroError_ = accepted ? "" : "Hero cannot reach that destination.";
    return accepted;
}

bool LevelRuntime::activateHeroAbility(HeroAbility ability) {
    return selectedHero_ && heroes_ &&
           heroCombat_.activate(*heroes_,
                                *selectedHero_,
                                ability,
                                level_.waves() && level_.waves()->phase() == LevelPhase::battle);
}

void LevelRuntime::cancelHeroOrder() {
    if (heroes_ && selectedHero_) {
        (void)heroes_->cancelCommand(*selectedHero_);
    }
}

const Tower* LevelRuntime::inspectedTower() const noexcept {
    for (const auto& tower : navigation_.towers().towers()) {
        if (inspectedTower_ == tower.object) {
            return &tower;
        }
    }
    return nullptr;
}

void LevelRuntime::cancelPlacement() noexcept {
    deployingHero_ = false;
    deployingId_.reset();
    if (heroes_) {
        heroes_->preview({});
    }
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
    if (placementError_.empty() && heroes_) {
        placementError_ = heroes_->towerError(*preview_);
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
