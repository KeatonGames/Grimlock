#pragma once

#include "input/ground_command.hpp"
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
    void towerInfo();
    void heroControls();
    void result();
    void placementOverlay(const Camera& camera);
    Engine& engine_;
    LevelRuntime& runtime_;
    bool viewportMouse_ = false, restart_ = false;
    std::string error_;
    std::optional<ObjectId> upgradedTower_;
    double upgradeFlashRemaining_ = 0;
    GroundCommandGesture commandGesture_;
    std::optional<glm::vec2> commandCursor_;
};

} // namespace grimlock::game
