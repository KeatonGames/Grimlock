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
    const auto* scene = engine.scene();
    const auto* grid = scene ? scene->navigationGrid() : nullptr;
    const bool replaced = scene_ != scene || grid_ != grid;
    const bool resized = grid && (!configuration_ || *configuration_ != grid->configuration());
    if (replaced || resized) {
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
    if (grid && geometryRevision_ != grid->revision()) {
        buildNavigation();
    } else if (!grid && (replaced || navigation_.ready())) {
        navigation_.reset();
        for (auto& profile : profiles_) {
            if (profile.mesh->revision() > 0) {
                profile.mesh->reset();
            }
        }
        geometry_ = {};
        geometryRevision_.reset();
    }
}

void NavigationSession::buildNavigation() {
    if (!grid_) {
        navigation_.reset();
        for (auto& profile : profiles_) {
            if (profile.mesh->revision() > 0) {
                profile.mesh->reset();
            }
        }
        geometry_ = {};
        return;
    }
    geometry_ = navigationGeometry(terrainBoxes(*grid_));
    buildProfile(0);
    for (const auto& profile : profiles_) {
        if (profile.mesh->revision() > 0 || typeProfiles_[profile.type] == typeProfiles_[selectedType_]) {
            buildProfile(profile.type);
        }
    }
    geometryRevision_ = grid_->revision();
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
}

void NavigationSession::selectAnimal(Engine& engine, EnemyTypeId type) {
    synchronize(engine);
    auto& surface = navigationFor(type);
    selectedType_ = type;
    if (grid_ && surface.revision() == 0) {
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
    // Synchronize the default without overwriting existing individual assignments.
    if (enemies_->sharedDestination() != destination_) {
        enemies_->setSharedDestination(destination_);
    }
    return enemies_->spawn(navigationFor(selectedType_), spawnArea_, count, selectedType_);
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
    selectedType_ = 0;
    geometryRevision_.reset();
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
