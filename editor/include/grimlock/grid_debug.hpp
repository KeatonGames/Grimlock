#pragma once

#include <glm/vec2.hpp>
#include <grimlock/grid.hpp>
#include <optional>
#include <span>

namespace grimlock {
class Engine;
}

namespace grimlock::editor {

// Non-owning Game debug data; Editor owns only projection and drawing.
struct GridDebugView {
    std::span<const glm::vec2> directions;
    std::span<const double> costs;
    std::optional<GridCell> destination;
    bool showDirections = false, showCosts = false;
};

class GridDebug {
public:
    virtual ~GridDebug() = default;
    virtual void menu(Engine& engine) = 0;
    virtual GridDebugView view(Engine& engine) = 0;
    virtual void update(Engine& engine, double delta) = 0;
};

} // namespace grimlock::editor
