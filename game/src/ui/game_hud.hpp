#pragma once

#include "runtime/level_runtime.hpp"

#include <grimlock/imgui_overlay.hpp>

namespace grimlock::game {

class GameHud final : public ui::ImGuiOverlay {
public:
    GameHud(Engine& engine, LevelRuntime& runtime);
    ~GameHud() override;
    OverlayInput frame() override;
    void finishFrame(const Camera& camera) override;

    bool requestClose() override {
        return true;
    }

private:
    void configureUi() override;
    void status();
    void construction();
    void result();
    void placementOverlay(const Camera& camera);
    Engine& engine_;
    LevelRuntime& runtime_;
    bool viewportMouse_ = false, restart_ = false;
    std::string error_;
};

} // namespace grimlock::game
