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
        enemies_ = std::make_unique<EnemySystem>(engine);
        engine_ = &engine;
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
        geometryRevision_.reset();
    }
}

void NavigationSession::buildNavigation() {
    if (!grid_) {
        navigation_.reset();
        return;
    }
    NavMeshConfiguration configuration;
    configuration.agentRadius = static_cast<float>(enemies_->definition().radius);
    const auto boxes = terrainBoxes(*grid_);
    navigation_.build(navigationGeometry(boxes), configuration);
    geometryRevision_ = grid_->revision();
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
    return enemies_->spawn(navigation_, spawnArea_, count);
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
    geometryRevision_.reset();
}

void NavigationSession::setDestination(std::optional<glm::dvec2> destination) {
    if (destination && (!std::isfinite(destination->x) || !std::isfinite(destination->y))) {
        throw std::invalid_argument("Enemy destination must contain finite world X/Z coordinates.");
    }
    destination_ = destination;
}

} // namespace grimlock::game
