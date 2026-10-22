#include "systems/level_session.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace grimlock::game {

void LevelSession::begin(Engine& engine, NavigationSession& navigation, LevelDefinition definition) {
    validateLevelDefinition(definition);
    navigation.synchronize(engine);
    auto& enemies = *navigation.enemies();
    std::vector<EnemyTypeId> types;
    std::vector<SpawnRoute> routes;
    for (const auto& wave : definition.waves) {
        for (const auto& group : wave.groups) {
            EnemyTypeId type = 0;
            while (type < enemies.typeCount() && enemies.definition(type).name != group.enemy) {
                ++type;
            }
            if (type == enemies.typeCount() || !enemies.definition(type).despawnOnArrival) {
                throw std::invalid_argument("Wave requires an available enemy with despawn_on_arrival enabled: " +
                                            group.enemy);
            }
            const auto& mesh = navigation.surface(engine, type);
            const auto area = definition.spawns[group.spawn].area;
            const glm::dvec3 goal{definition.objectivePosition.x, area.height, definition.objectivePosition.y};
            if (!mesh.contains(goal) || !TowerSystem::routeFromArea(mesh, area, goal)) {
                throw std::invalid_argument("Wave spawn cannot reach the objective: " + group.enemy);
            }
            types.push_back(type);
            routes.push_back({type, area, definition.objectivePosition});
        }
    }
    WaveSystem prepared{std::move(definition)};
    combat_.reset();
    navigation.reset(engine);
    combat_.emplace(engine);
    navigation.setSpawnRoutes(std::move(routes));
    navigation.spawnArea() = prepared.definition().spawns.front().area;
    navigation.setDestination(prepared.definition().objectivePosition);
    waves_ = std::move(prepared);
    economy_.reset(waves_->definition().startingGold);
    lastWaveReward_.reset();
    types_ = std::move(types);
    scene_ = engine.scene();
    arrivals_ = 0;
    navigation.setPlacementEnabled(true);
}

void LevelSession::stop(NavigationSession& navigation) {
    combat_.reset();
    waves_.reset();
    economy_.reset();
    lastWaveReward_.reset();
    types_.clear();
    scene_ = nullptr;
    navigation.setSpawnRoutes({});
    navigation.setPlacementEnabled(true);
}

void LevelSession::startWave(NavigationSession& navigation) {
    if (!waves_) {
        throw std::logic_error("Load a level before starting a wave.");
    }
    waves_->startWave();
    navigation.setPlacementEnabled(false);
}

void LevelSession::update(Engine& engine, NavigationSession& navigation, double delta) {
    if (!std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Level delta must be finite and nonnegative.");
    }
    if (!waves_) {
        navigation.update(engine, delta);
        return;
    }
    if (engine.scene() != scene_) {
        stop(navigation);
        navigation.update(engine, delta);
        return;
    }
    if (waves_->phase() != LevelPhase::battle) {
        navigation.setPlacementEnabled(waves_->phase() == LevelPhase::preparation);
        navigation.synchronize(engine);
        navigation.enemies()->updateEffects(delta);
        return;
    }
    // Advance movement, attacks and the authored schedule together on slow frames.
    do {
        const double step = std::min(delta, 1.0 / 30);
        updateBattle(engine, navigation, step);
        delta -= step;
    } while (delta > 1e-10 && waves_->phase() == LevelPhase::battle);
}

void LevelSession::updateBattle(Engine& engine, NavigationSession& navigation, double delta) {
    navigation.update(engine, delta);
    combat_->update(navigation.towers(), *navigation.enemies(), delta);
    const auto arrived = navigation.enemies()->metrics().arrived;
    const auto newArrivals = arrived >= arrivals_ ? arrived - arrivals_ : 0;
    arrivals_ = arrived;
    const auto& definition = waves_->definition();
    std::size_t offset = 0;
    for (std::size_t wave = 0; wave < waves_->wave(); ++wave) {
        offset += definition.waves[wave].groups.size();
    }
    const auto completedWave = waves_->wave();
    const auto& groups = definition.waves[completedWave].groups;
    waves_->update(
        delta,
        navigation.enemies()->enemies().size(),
        newArrivals,
        [&](const WaveGroup& group, std::size_t count) {
            const auto index = static_cast<std::size_t>(&group - groups.data());
            return navigation.spawnAt(engine, types_[offset + index], definition.spawns[group.spawn].area, count);
        });
    if (waves_->wave() != completedWave) {
        const auto reward = definition.waves[completedWave].goldReward;
        economy_.earn(reward);
        lastWaveReward_ = reward;
    }
    navigation.setPlacementEnabled(waves_->phase() == LevelPhase::preparation);
    if (waves_->phase() != LevelPhase::battle) {
        combat_->clearProjectiles();
    }
}

std::string LevelSession::constructionError(const NavigationSession& navigation, const Tower& tower) const {
    if (!waves_ || waves_->phase() != LevelPhase::preparation) {
        return "Construction is only available during level preparation.";
    }
    const auto cost = navigation.towers().definition(tower.type).constructionCost;
    return economy_.canAfford(cost) ? std::string{} : "Insufficient gold to construct this tower.";
}

ObjectId LevelSession::buildTower(Engine& engine, NavigationSession& navigation, const Tower& tower) {
    if (engine.scene() != scene_) {
        throw std::logic_error("Load the level before constructing towers.");
    }
    navigation.synchronize(engine);
    if (const auto error = constructionError(navigation, tower); !error.empty()) {
        throw std::invalid_argument(error);
    }
    const auto cost = navigation.towers().definition(tower.type).constructionCost;
    const auto object = navigation.placeTower(engine, tower);
    // Placement may fail or throw; currency changes only after its scene/navigation commit succeeds.
    if (!economy_.trySpend(cost)) {
        throw std::logic_error("Gold changed during a synchronous tower placement.");
    }
    return object;
}

} // namespace grimlock::game
