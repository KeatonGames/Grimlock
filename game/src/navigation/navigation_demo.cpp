#include "navigation/navigation_demo.hpp"

#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>

namespace grimlock::game {

void NavigationDemo::synchronize(Engine& engine) {
    const auto* scene = engine.scene();
    const auto* grid = scene ? scene->navigationGrid() : nullptr;
    if (engine_ != &engine) {
        agents_.clear();
        cube_.reset();
        scene_ = nullptr;
        grid_ = nullptr;
        field_.invalidate();
        destination_.reset();
        engine_ = &engine;
    }

    if (scene_ != scene || grid_ != grid) {
        reset(engine);
        scene_ = scene;
        grid_ = grid;
        field_.invalidate();
        destination_ = grid ? std::optional{GridCell{grid->configuration().width - 1, grid->configuration().height - 1}}
                            : std::nullopt;
    }

    if (grid) {
        field_.update(*grid, destination_);
    }
}

void NavigationDemo::update(Engine& engine, double delta) {
    synchronize(engine);

    if (!grid_) {
        return;
    }

    const auto& c = grid_->configuration();
    // The circumscribed X/Z square of a rotated cube fits inside this clearance.
    const double halfSize = c.cellSize * 0.16;
    for (auto& agent : agents_) {
        agent.movement.radius = halfSize * std::sqrt(2.0);
        agent.movement.position.y = c.origin.y + halfSize;
        advanceAgent(*grid_, field_, agent.movement, delta);

        Transform transform;
        transform.position = glm::vec3{agent.movement.position};
        transform.scale = glm::vec3{static_cast<float>(halfSize)};
        transform.rotation = glm::angleAxis(static_cast<float>(agent.movement.yaw), glm::vec3{0, 1, 0});
        engine.setTransform(agent.instance, transform);
    }
}

void NavigationDemo::spawn(Engine& engine) {
    synchronize(engine);

    if (!grid_ || !agents_.empty()) {
        return;
    }

    if (!cube_) {
        cube_ = engine.loadModel(engine.assetRoot() / "models/cube.obj");
    }

    agents_.reserve(6);
    for (std::size_t index = 0; index < grid_->size() && agents_.size() < 6; ++index) {
        if (index < agents_.size() * grid_->size() / 6) {
            continue;
        }

        const auto cell = *grid_->cell(index);
        if (!grid_->walkable(cell) || destination_ == cell) {
            continue;
        }

        TestAgent agent;
        agent.movement.position = *grid_->cellCenter(cell);
        agent.movement.speed = speed_ * (0.75 + 0.1 * static_cast<double>(agents_.size()));
        agent.instance = engine.createInstance(*cube_, true);
        agents_.push_back(agent);
        engine.setTint(agent.instance, {0.15F + 0.08F * static_cast<float>(agents_.size()), 0.65F, 0.20F, 1});
    }

    update(engine, 0);
}

void NavigationDemo::reset(Engine& engine) {
    for (const auto& agent : agents_) {
        engine.removeInstance(agent.instance);
    }
    agents_.clear();
}

void NavigationDemo::setSpeed(double speed) {
    if (!std::isfinite(speed) || speed < 0 || speed > 100) {
        throw std::invalid_argument("Test agent speed must be finite and between 0 and 100 world units per second.");
    }
    speed_ = speed;
    for (std::size_t index = 0; index < agents_.size(); ++index) {
        agents_[index].movement.speed = speed_ * (0.75 + 0.1 * static_cast<double>(index));
    }
}

} // namespace grimlock::game
