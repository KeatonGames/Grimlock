#pragma once

#include "navigation/agent.hpp"

#include <grimlock/engine.hpp>

namespace grimlock::game {

struct TestAgent {
    NavigationAgent movement;
    InstanceId instance;
};

class NavigationDemo final {
public:
    void synchronize(Engine& engine);
    void update(Engine& engine, double delta);
    void spawn(Engine& engine);
    void reset(Engine& engine);

    void setDestination(std::optional<GridCell> destination) noexcept {
        destination_ = destination;
    }

    [[nodiscard]] std::optional<GridCell> destination() const noexcept {
        return destination_;
    }

    [[nodiscard]] const FlowField& field() const noexcept {
        return field_;
    }

    [[nodiscard]] std::span<const TestAgent> agents() const noexcept {
        return agents_;
    }

    void setSpeed(double speed);

    [[nodiscard]] double speed() const noexcept {
        return speed_;
    }

private:
    Engine* engine_ = nullptr;
    const Scene* scene_ = nullptr;
    const Grid* grid_ = nullptr;
    std::optional<GridCell> destination_;
    FlowField field_;
    std::optional<ModelId> cube_;
    std::vector<TestAgent> agents_;
    double speed_ = 1.5;
};

} // namespace grimlock::game
