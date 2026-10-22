#include "systems/level_session.hpp"

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
    navigation.reset(engine);
    navigation.setSpawnRoutes(std::move(routes));
    navigation.spawnArea() = prepared.definition().spawns.front().area;
    navigation.setDestination(prepared.definition().objectivePosition);
    waves_ = std::move(prepared);
    types_ = std::move(types);
    scene_ = engine.scene();
    arrivals_ = 0;
    navigation.setPlacementEnabled(true);
}

void LevelSession::stop(NavigationSession& navigation) {
    waves_.reset();
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
        return;
    }
    navigation.update(engine, delta);
    const auto arrived = navigation.enemies()->metrics().arrived;
    const auto newArrivals = arrived >= arrivals_ ? arrived - arrivals_ : 0;
    arrivals_ = arrived;
    const auto& definition = waves_->definition();
    std::size_t offset = 0;
    for (std::size_t wave = 0; wave < waves_->wave(); ++wave) {
        offset += definition.waves[wave].groups.size();
    }
    const auto& groups = definition.waves[waves_->wave()].groups;
    waves_->update(
        delta,
        navigation.enemies()->enemies().size(),
        newArrivals,
        [&](const WaveGroup& group, std::size_t count) {
            const auto index = static_cast<std::size_t>(&group - groups.data());
            return navigation.spawnAt(engine, types_[offset + index], definition.spawns[group.spawn].area, count);
        });
    navigation.setPlacementEnabled(waves_->phase() == LevelPhase::preparation);
}

} // namespace grimlock::game
