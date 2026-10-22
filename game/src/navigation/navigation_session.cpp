#include "navigation/navigation_session.hpp"

#include <algorithm>
#include <cmath>
#include <grimlock/terrain.hpp>
#include <stdexcept>

namespace grimlock::game {

void NavigationSession::synchronize(Engine& engine) {
    if (engine_ && engine_ != &engine) {
        throw std::logic_error("Shut down the navigation session before using another Engine.");
    }
    if (!enemies_) {
        const auto definitions = loadAnimalDefinitions(engine.assetRoot());
        enemies_ = std::make_unique<EnemySystem>(engine, definitions.front());
        engine_ = &engine;
        typeProfiles_.push_back(0);
        for (std::size_t type = 1; type < definitions.size(); ++type) {
            (void)enemies_->addDefinition(definitions[type]);
            std::size_t matching = 0;
            while (matching < type && (definitions[matching].radius != definitions[type].radius ||
                                       definitions[matching].height != definitions[type].height)) {
                ++matching;
            }
            if (matching < type) {
                typeProfiles_.push_back(typeProfiles_[matching]);
            } else {
                profiles_.push_back({type, std::make_unique<NavMesh>()});
                typeProfiles_.push_back(profiles_.size());
            }
        }
    }
    towers_.synchronize(engine);
    placement_.synchronize(engine.scene());
    const auto* scene = engine.scene();
    const auto* grid = scene ? scene->navigationGrid() : nullptr;
    const bool replaced = scene_ != scene || grid_ != grid;
    const bool resized = grid && (!configuration_ || *configuration_ != grid->configuration());
    if (replaced || resized) {
        spawnRoutes_.clear();
        enemies_->clear();
        scene_ = scene;
        grid_ = grid;
        geometryRevision_.reset();
        if (replaced) {
            destination_ = glm::dvec2{4, 4};
        }
        configuration_ = grid ? std::optional{grid->configuration()} : std::nullopt;
        if (grid) {
            const auto& c = grid->configuration();
            const double s = c.cellSize;
            spawnArea_ = {{c.origin.x + s * 0.5, c.origin.z + s * 0.5},
                          {c.origin.x + std::max(1U, c.width / 3) * s - s * 0.5, c.origin.z + c.height * s - s * 0.5},
                          c.origin.y};
            if (replaced) {
                destination_ = glm::dvec2{c.origin.x + (c.width - 0.5) * s, c.origin.z + (c.height - 0.5) * s};
            }
        } else {
            spawnArea_ = {{-4, -4}, {4, 4}};
        }
    }
    if (grid && (geometryRevision_ != grid->revision() || towerRevision_ != towers_.revision())) {
        buildNavigation();
    } else if (!grid && (replaced || navigation_.ready())) {
        navigation_.reset();
        for (auto& profile : profiles_) {
            if (profile.mesh->revision() > 0) {
                profile.mesh->reset();
            }
        }
        geometry_ = {};
        terrain_.clear();
        geometryRevision_.reset();
    }
}

void NavigationSession::buildNavigation() {
    candidate_.reset();
    ++navigationRevision_;
    if (!grid_) {
        navigation_.reset();
        for (auto& profile : profiles_) {
            if (profile.mesh->revision() > 0) {
                profile.mesh->reset();
            }
        }
        geometry_ = {};
        terrain_.clear();
        return;
    }
    terrain_ = terrainBoxes(*grid_);
    geometry_ = navigationGeometry(terrain_);
    for (const auto& tower : towers_.towers()) {
        const auto box = tower.bounds();
        geometry_.addBox(box.center - box.halfExtent, box.center + box.halfExtent, false);
    }
    if (navigation_.ready() || profileInUse(0)) {
        buildProfile(0);
    }
    for (const auto& profile : profiles_) {
        if (profile.mesh->ready() || profileInUse(profile.type)) {
            buildProfile(profile.type);
        }
    }
    geometryRevision_ = grid_->revision();
    towerRevision_ = towers_.revision();
}

NavMesh& NavigationSession::navigationFor(EnemyTypeId type) {
    const auto profile = typeProfiles_.at(type);
    return profile == 0 ? navigation_ : *profiles_[profile - 1].mesh;
}

void NavigationSession::buildProfile(EnemyTypeId type) {
    NavMeshConfiguration configuration;
    const auto& definition = enemies_->definition(type);
    configuration.agentRadius = static_cast<float>(definition.radius);
    configuration.agentHeight = static_cast<float>(definition.height);
    navigationFor(type).build(geometry_, configuration);
    ++navigationRevision_;
}

void NavigationSession::selectAnimal(Engine& engine, EnemyTypeId type) {
    synchronize(engine);
    auto& surface = navigationFor(type);
    selectedType_ = type;
    if (grid_ && !surface.ready()) {
        buildProfile(type);
    }
}

void NavigationSession::rebuild(Engine& engine) {
    synchronize(engine);
    buildNavigation();
}

void NavigationSession::update(Engine& engine, double delta) {
    synchronize(engine);
    enemies_->update(navigation_, destination_, delta);
}

std::size_t NavigationSession::spawn(Engine& engine, std::size_t count) {
    synchronize(engine);
    return spawnAt(engine, selectedType_, spawnArea_, count);
}

const NavMesh& NavigationSession::surface(Engine& engine, EnemyTypeId type) {
    synchronize(engine);
    if (!navigationFor(type).ready() && grid_) {
        buildProfile(type);
    }
    return navigationFor(type);
}

void NavigationSession::setSpawnRoutes(std::vector<SpawnRoute> routes) {
    for (const auto& route : routes) {
        if (route.type >= typeProfiles_.size()) {
            throw std::invalid_argument("Unknown enemy type in spawn route.");
        }
    }
    spawnRoutes_ = std::move(routes);
    candidate_.reset();
}

std::size_t NavigationSession::spawnAt(Engine& engine, EnemyTypeId type, SpawnArea area, std::size_t count) {
    const auto& mesh = surface(engine, type);
    // Synchronize the default without overwriting existing individual assignments.
    if (enemies_->sharedDestination() != destination_) {
        enemies_->setSharedDestination(destination_);
    }
    return enemies_->spawn(mesh, area, count, type);
}

void NavigationSession::reset(Engine& engine) {
    synchronize(engine);
    enemies_->clear();
}

void NavigationSession::shutdown() noexcept {
    enemies_.reset();
    engine_ = nullptr;
    scene_ = nullptr;
    grid_ = nullptr;
    configuration_.reset();
    destination_.reset();
    navigation_.reset();
    profiles_.clear();
    typeProfiles_.clear();
    geometry_ = {};
    spawnRoutes_.clear();
    selectedType_ = 0;
    geometryRevision_.reset();
    candidate_.reset();
    towers_.clear();
    placement_.synchronize(nullptr);
    placementEnabled_ = true;
    terrain_.clear();
    towerRevision_ = 0;
    ++navigationRevision_;
}

std::optional<Tower> NavigationSession::towerAt(const Ray& ray, const std::string& type, double yaw) const {
    const auto hit = TowerSystem::terrainHit(ray, terrain_);
    if (!hit) {
        return {};
    }
    Tower tower = towers_.definition(type).instance();
    tower.rotation = glm::angleAxis(yaw, glm::dvec3{0, 1, 0});
    tower.position = *hit + glm::dvec3{0, tower.halfExtent.y, 0};
    return tower;
}

void NavigationSession::preparePlacement(const Tower& tower) {
    candidate_.emplace(
        PlacementCandidate{tower, spawnArea_, *destination_, selectedType_, navigationRevision_, geometry_, {}, {}});
    auto& candidate = *candidate_;
    const auto box = tower.bounds();
    candidate.geometry.addBox(box.center - box.halfExtent, box.center + box.halfExtent, false);
    const auto prepare = [&](EnemyTypeId type) {
        auto& current = navigationFor(type);
        auto trial = std::make_unique<NavMesh>();
        const bool selected = typeProfiles_[type] == typeProfiles_[selectedType_];
        const auto goal = current.project({destination_->x, spawnArea_.height, destination_->y});
        const bool hadRoute = goal && TowerSystem::routeFromArea(current, spawnArea_, goal->position);
        if (selected && !hadRoute) {
            candidate.error = "Configure a reachable spawn area and destination before placing towers.";
            return;
        }
        if (!trial->build(candidate.geometry, current.configuration())) {
            if (selected) {
                candidate.error = "Cannot build navigation around this tower: " + trial->status();
            }
            candidate.profiles.push_back({type, std::move(trial), hadRoute, false});
            return;
        }
        const bool hasRoute = hadRoute && trial->contains(goal->position) &&
                              TowerSystem::routeFromArea(*trial, spawnArea_, goal->position);
        if (selected && !hasRoute) {
            candidate.error = "Tower would close every spawn-to-destination route for the selected animal profile.";
        }
        candidate.profiles.push_back({type, std::move(trial), hadRoute, hasRoute});
    };
    if (profileInUse(0)) {
        prepare(0);
    }
    for (const auto& profile : profiles_) {
        if (!candidate.error.empty()) {
            break;
        }
        if (profileInUse(profile.type)) {
            prepare(profile.type);
        }
    }
}

bool NavigationSession::profileInUse(EnemyTypeId type) const {
    const auto profile = typeProfiles_[type];
    if (profile == typeProfiles_[selectedType_] ||
        std::any_of(spawnRoutes_.begin(), spawnRoutes_.end(), [&](const SpawnRoute& route) {
            return typeProfiles_[route.type] == profile;
        })) {
        return true;
    }
    const auto* surface = profile == 0 ? &navigation_ : profiles_[profile - 1].mesh.get();
    return std::any_of(enemies_->enemies().begin(), enemies_->enemies().end(), [&](const Enemy& enemy) {
        return enemy.active && enemy.alive && (enemy.navigation ? enemy.navigation : &navigation_) == surface;
    });
}

std::string NavigationSession::checkEnemyRoutes(const PlacementCandidate& candidate) const {
    NavigationPath path;
    for (const auto& prepared : candidate.profiles) {
        const auto* current =
            typeProfiles_[prepared.type] == 0 ? &navigation_ : profiles_[typeProfiles_[prepared.type] - 1].mesh.get();
        for (const auto& route : spawnRoutes_) {
            if (typeProfiles_[route.type] != typeProfiles_[prepared.type]) {
                continue;
            }
            const glm::dvec3 goal{route.destination.x, route.area.height, route.destination.y};
            if (!prepared.mesh->contains(goal) || !TowerSystem::routeFromArea(*prepared.mesh, route.area, goal)) {
                return "Tower would close a predefined wave's spawn-to-objective route.";
            }
        }
        for (const auto& enemy : enemies_->enemies()) {
            if (!enemy.active || !enemy.alive || (enemy.navigation ? enemy.navigation : &navigation_) != current) {
                continue;
            }
            if (prepared.hadSpawnRoute && !prepared.hasSpawnRoute) {
                return "Tower would close every spawn-to-destination route for an active animal profile.";
            }
            if (current->contains(enemy.position) && !prepared.mesh->contains(enemy.position)) {
                return "Tower would remove navigation clearance beneath an animal.";
            }
            if (enemy.destination) {
                const auto goal = current->project({enemy.destination->x, enemy.position.y, enemy.destination->y});
                const bool hadRoute = (enemy.path.valid && enemy.path.revision == current->revision()) ||
                                      (goal && current->findPath(enemy.position, goal->position, path));
                if (hadRoute && goal &&
                    (!prepared.mesh->contains(goal->position) ||
                     !prepared.mesh->findPath(enemy.position, goal->position, path))) {
                    return "Tower would cut off an animal's assigned destination.";
                }
            }
        }
    }
    return {};
}

std::string NavigationSession::checkTower(Engine& engine, const Tower& tower, bool checkRoutes) {
    synchronize(engine);
    if (!placementEnabled_) {
        return "Towers can only be placed during preparation.";
    }
    if (const auto zoneError = placement_.check(tower); !zoneError.empty()) {
        return zoneError;
    }
    if (!grid_ || !navigation().ready()) {
        return "Tower placement requires navigable terrain.";
    }
    const auto error = towers_.check(tower, terrain_, enemies_->enemies());
    if (!error.empty()) {
        return error;
    }
    if (!destination_) {
        return "Assign a destination before placing towers.";
    }
    if (!checkRoutes) {
        return {};
    }
    const auto covered = [&](EnemyTypeId type) {
        return !profileInUse(type) ||
               (candidate_ && std::any_of(candidate_->profiles.begin(),
                                          candidate_->profiles.end(),
                                          [&](const PreparedProfile& prepared) {
                                              return typeProfiles_[prepared.type] == typeProfiles_[type];
                                          }));
    };
    const bool profilesCovered =
        covered(0) && std::all_of(profiles_.begin(), profiles_.end(), [&](const NavigationProfile& profile) {
            return covered(profile.type);
        });
    if (!candidate_ || candidate_->tower != tower || candidate_->revision != navigationRevision_ ||
        candidate_->area.minimum != spawnArea_.minimum || candidate_->area.maximum != spawnArea_.maximum ||
        candidate_->area.height != spawnArea_.height || candidate_->destination != *destination_ ||
        candidate_->type != selectedType_ || !profilesCovered) {
        preparePlacement(tower);
    }
    return candidate_->error.empty() ? checkEnemyRoutes(*candidate_) : candidate_->error;
}

ObjectId NavigationSession::placeTower(Engine& engine, const Tower& tower) {
    const auto error = checkTower(engine, tower);
    if (!error.empty()) {
        throw std::invalid_argument(error);
    }
    auto prepared = std::move(*candidate_);
    candidate_.reset();
    const auto id = towers_.create(engine, tower);
    for (auto& profile : prepared.profiles) {
        navigationFor(profile.type).replaceWith(std::move(*profile.mesh));
    }
    // Unused profiles must not retain geometry from before this placement. Rebuild them only when requested.
    if (!profileInUse(0)) {
        navigation_.reset();
    }
    for (const auto& profile : profiles_) {
        if (!profileInUse(profile.type)) {
            profile.mesh->reset();
        }
    }
    geometry_ = std::move(prepared.geometry);
    geometryRevision_ = grid_->revision();
    towerRevision_ = towers_.revision();
    ++navigationRevision_;
    return id;
}

bool NavigationSession::removeTower(Engine& engine, ObjectId object) {
    synchronize(engine);
    if (!towers_.remove(engine, object)) {
        return false;
    }
    synchronize(engine);
    return true;
}

void NavigationSession::setDestination(std::optional<glm::dvec2> destination) {
    if (destination && (!std::isfinite(destination->x) || !std::isfinite(destination->y))) {
        throw std::invalid_argument("Enemy destination must contain finite world X/Z coordinates.");
    }
    destination_ = destination;
    if (enemies_) {
        enemies_->setSharedDestination(destination);
    }
}

} // namespace grimlock::game
