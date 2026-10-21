#pragma once

#include "scene_document.hpp"

struct SDL_Window;

namespace grimlock::editor {

class GridDebug;

void drawDebugGeometry(const Camera& camera, SDL_Window* window, const GridDebug* debug);

// Session-only debug tools; authored data stays in the real Scene/SceneDocument.
class GridTools final {
public:
    void menu(Engine& engine, GridDebug* debug = nullptr);
    void dialog(SceneDocument& document);
    void finishFrame(Engine& engine,
                     SceneDocument& document,
                     const Camera& camera,
                     SDL_Window* window,
                     bool viewportMouse);

    [[nodiscard]] bool editing() const noexcept {
        return editing_;
    }

    void resetInteraction() noexcept {
        editing_ = false;
        lastCell_.reset();
    }

private:
    bool editing_ = false, paintWalkable_ = false, openSettings_ = false;
    int dimensions_[2]{16, 16};
    double cellSize_ = 1;
    glm::dvec3 origin_{0};
    std::string error_;
    std::optional<GridCell> lastCell_;
};

} // namespace grimlock::editor
