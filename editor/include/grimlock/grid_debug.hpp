#pragma once

#include <array>
#include <glm/vec3.hpp>
#include <span>

namespace grimlock {
class Engine;
}

namespace grimlock::editor {

// Game-owned development controls hosted in the existing terrain tools menu.
class GridDebug {
public:
    virtual ~GridDebug() = default;
    virtual void menu(Engine& engine) = 0;
    virtual void update(Engine& engine, double delta) = 0;

    // Optional world-space development geometry; Editor owns projection/drawing.
    [[nodiscard]] virtual std::span<const std::array<glm::vec3, 3>> triangles() const noexcept {
        return {};
    }

    // Called before the attached Engine is destroyed; release owned preview instances here.
    virtual void shutdown(Engine&) noexcept {
    }
};

} // namespace grimlock::editor
