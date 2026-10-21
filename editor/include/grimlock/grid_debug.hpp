#pragma once

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

    // Called before the attached Engine is destroyed; release owned preview instances here.
    virtual void shutdown(Engine&) noexcept {
    }
};

} // namespace grimlock::editor
