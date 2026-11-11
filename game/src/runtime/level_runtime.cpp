#include "runtime/level_runtime.hpp"

#include "input/ground_command.hpp"

#include <algorithm>
#include <cmath>
#include <grimlock/ray.hpp>
#include <stdexcept>

namespace grimlock::game {

std::filesystem::path assetRootForLevel(const std::filesystem::path& level) {
    if (level.is_absolute() && level.parent_path().filename() == "levels" &&
        level.parent_path().parent_path().filename() == "test_fixtures") {
        return level.parent_path().parent_path().parent_path();
    }
    if (level.is_absolute() && level.parent_path().filename() == "levels") {
        return level.parent_path().parent_path();
    }
#ifdef GRIMLOCK_SOURCE_ASSET_ROOT
    const std::filesystem::path source{GRIMLOCK_SOURCE_ASSET_ROOT};
    if (std::filesystem::is_directory(source / "levels") && std::filesystem::is_directory(source / "scenes")) {
        return source;
    }
#endif
    return "assets";
}

void LevelRuntime::spawnHeroesNearObjective() {
    const auto& levelDefinition = level_.waves()->definition();
    const glm::dvec3 objective{levelDefinition.objectivePosition.x,
                               levelDefinition.objectiveHeight,
                               levelDefinition.objectivePosition.y};
    for (std::size_t type = 0; type < heroes_->typeCount(); ++type) {
        const auto& mesh = heroes_->navigation(type);
        const auto anchor = mesh.project(objective, 2);
        bool spawned = false;
        NavigationPath path;
        for (double radius = 1.2; anchor && radius <= 6 && !spawned; radius += 0.6) {
            for (int step = 0; step < 24 && !spawned; ++step) {
                const auto angle = 2 * std::acos(-1.0) *
                                   (static_cast<double>(type) / heroes_->typeCount() + static_cast<double>(step) / 24);
                const auto desired = objective + glm::dvec3{std::cos(angle) * radius, 0, std::sin(angle) * radius};
                const auto point = mesh.project(desired, 0.4);
                if (point && std::abs(point->position.y - objective.y) < 0.6 &&
                    heroes_->validPosition(point->position, {}, type) &&
                    mesh.findPath(anchor->position, point->position, path)) {
                    (void)heroes_->deploy(point->position, {}, type);
                    spawned = true;
                }
            }
        }
        if (!spawned) {
            throw std::runtime_error("No clear ground near the objective for " + heroes_->definition(type).name);
        }
    }
}

void LevelRuntime::load(Engine& engine, const std::filesystem::path& level) {
    const auto path = level.is_absolute() ? level : engine.assetRoot().parent_path() / level;
    auto definition = loadLevelDefinition(path);
    auto scene = engine.loadScene(engine.assetRoot() / definition.scene);
    unload(engine);
    try {
        engine.setScene(std::move(scene));
        level_.begin(engine, navigation_, std::move(definition));
        engine_ = &engine;
        heroes_ =
            std::make_unique<HeroSystem>(engine,
                                         std::span<const std::string>{level_.waves()->definition().availableHeroes});
        heroes_->shareCombatGeometry(navigation_.combatGeometry());
        heroes_->synchronize(navigation_.towers());
        spawnHeroesNearObjective();
        for (const auto& [id, object] : engine.scene()->objects()) {
            engine.scene()->setSelectionHighlight(id, false);
        }
        levelPath_ = path;
        if (progress_) {
            progress_->startAttempt(level_.waves()->definition().id);
        }
        engine.setObjectSelectionEnabled(false);
        engine.setCameraNavigationEnabled();
        auto camera = engine.cameraNavigationConfig();
        camera.panDragThreshold = GroundCommandGesture::dragThreshold;
        engine.configureCameraNavigation(camera);
    } catch (...) {
        unload(engine);
        throw;
    }
}

void LevelRuntime::unload(Engine& engine) {
    paused_ = false;
    engine.setAnimationsPaused(false);
    if (progress_) {
        progress_->abandonAttempt();
    }
    elapsed_ = 0;
    resultRecorded_ = personalRecord_ = false;
    cancelPlacement();
    heroes_.reset();
    selectedHero_.reset();
    level_.stop(navigation_);
    heroError_.clear();
    inspectedTower_.reset();
    navigation_.shutdown();
    heroCombat_ = {};
    levelPath_.clear();
    engine_ = nullptr;
    selectedTower_ = "basic";
    engine.setScene(nullptr);
    engine.setObjectSelectionEnabled(false);
    engine.setCameraNavigationEnabled(false);
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
    if (!level_.waves() || paused_) {
        return;
    }
    do {
        const double step = std::min(delta, 1.0 / 30);
        const auto phase = level_.waves()->phase();
        if (phase == LevelPhase::preparation || phase == LevelPhase::battle) {
            elapsed_ += step;
        }
        level_.update(engine, navigation_, step, heroes_.get());
        if (heroes_) {
            heroes_->synchronize(navigation_.towers());
            heroCombat_.update(*heroes_,
                               *navigation_.enemies(),
                               step,
                               level_.waves() && level_.waves()->phase() == LevelPhase::battle,
                               level_.waves() && level_.waves()->phase() == LevelPhase::preparation);
            level_.collectKillRewards(*navigation_.enemies());
        }
        delta -= step;
        const auto current = level_.waves()->phase();
        if (!resultRecorded_ && (current == LevelPhase::victory || current == LevelPhase::defeat)) {
            resultRecorded_ = true;
            if (progress_) {
                personalRecord_ =
                    progress_->finishAttempt(level_.waves()->definition().id, current == LevelPhase::victory, elapsed_);
            }
        }
    } while (delta > 1e-10);
    if (!level_.waves() || level_.waves()->phase() != LevelPhase::preparation) {
        cancelPlacement();
    }
}

void LevelRuntime::startWave() {
    level_.startWave(navigation_);
    cancelPlacement();
}

void LevelRuntime::setPaused(bool paused) {
    paused_ = paused && level_.waves();
    if (engine_) {
        engine_->setAnimationsPaused(paused_);
        engine_->setCameraNavigationEnabled(!paused_);
    }
}

bool LevelRuntime::selectHero(HeroId id) {
    const auto* hero = heroes_ ? heroes_->find(id) : nullptr;
    if (!hero || !engine_ || paused_) {
        return false;
    }
    cancelPlacement();
    heroError_.clear();
    inspectedTower_.reset();
    selectedHero_ = id;
    engine_->scene()->selectObject(hero->object);
    return true;
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
        inspectedTower_ = navigation_.towers().entityForObject(*object);
    }
    if (selectedHero_ || inspectedTower_) {
        engine.scene()->setSelectionHighlight(*object, false);
        engine.scene()->selectObject(object);
    } else {
        engine.clearSelection();
    }
}

const Hero* LevelRuntime::selectedHero() const noexcept {
    return heroes_ && selectedHero_ ? heroes_->find(*selectedHero_) : nullptr;
}

bool LevelRuntime::commandHero(const Ray& ray) {
    if (paused_ || !selectedHero() || !level_.waves() ||
        (level_.waves()->phase() != LevelPhase::battle && level_.waves()->phase() != LevelPhase::preparation)) {
        return false;
    }
    if (engine_ && navigation_.enemies() && level_.waves()->phase() == LevelPhase::battle) {
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

void LevelRuntime::clearSelection(bool cancelOrder) {
    if (const auto* hero = selectedHero();
        cancelOrder && hero && (hero->combat.playerMoving || hero->combat.explicitTarget)) {
        cancelHeroOrder();
    }
    selectedHero_.reset();
    inspectedTower_.reset();
    heroError_.clear();
    if (engine_) {
        engine_->clearSelection();
    }
}

const Tower* LevelRuntime::inspectedTower() const noexcept {
    return inspectedTower_ ? navigation_.towers().find(*inspectedTower_) : nullptr;
}

void LevelRuntime::cancelPlacement() noexcept {
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
            const auto tower = level_.buildTower(engine, navigation_, *preview_);
            engine.scene()->setSelectionHighlight(tower, false);
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
