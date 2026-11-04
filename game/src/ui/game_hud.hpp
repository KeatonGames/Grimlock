#pragma once

#include "input/ground_command.hpp"
#include "runtime/level_catalog.hpp"
#include "runtime/level_runtime.hpp"

#include <grimlock/imgui_overlay.hpp>

namespace grimlock::game {

class GameHud final : public ui::ImGuiOverlay {
public:
    GameHud(Engine& engine,
            LevelRuntime& runtime,
            bool startInMenu = false,
            const std::filesystem::path& initialLevel = {});
    ~GameHud() override;
    OverlayInput frame() override;
    void finishFrame(const Camera& camera) override;

    bool requestClose() override {
        return true;
    }

    [[nodiscard]] bool quitRequested() const noexcept {
        return quit_;
    }

private:
    std::size_t heroType_ = 0;
    void configureUi() override;
    void transitions();
    void menu();
    void refreshLevels();
    void clearFeedback();
    void progressNotice();
    void status();
    void bossHealth();
    void construction();
    void towerInfo();
    void heroControls();
    void result();
    void placementOverlay(const Camera& camera);
    void pauseMenu();
    void settings();
    void notifications();
    void updateLayout();
    Engine& engine_;
    LevelRuntime& runtime_;
    bool viewportMouse_ = false, restart_ = false;
    std::string error_;
    std::optional<ObjectId> upgradedTower_;
    double upgradeFlashRemaining_ = 0;
    GroundCommandGesture commandGesture_;
    std::optional<glm::vec2> commandCursor_;

    struct BossBar {
        EnemyId enemy;
        float fraction = 1;
    };

    std::vector<BossBar> bossBars_;
    glm::vec2 statusSize_{0};
    enum class Screen {
        mainMenu,
        levels,
        gameplay
    };
    Screen screen_ = Screen::gameplay;
    std::optional<Screen> requestedScreen_;
    std::optional<std::filesystem::path> requestedLevel_, selectedLevel_;
    LevelCatalog catalog_;
    double refreshRemaining_ = 0;
    bool quit_ = false;
    bool resultCatalogReady_ = false;

    struct Layout {
        float scale = 1, gap = 16, top = 92;
        float dockWidth = 760, dockHeight = 212, contextWidth = 320, contextHeight = 270;
    } layout_;

    float uiScale_ = 1, entrance_ = 0;
    bool settingsOpen_ = false, worldHealthBars_ = true;
    std::optional<LevelPhase> displayedPhase_;
    std::size_t displayedWave_ = 0;
    std::string notification_;
    float notificationRemaining_ = 0;
    bool modalFrame_ = false;
};

} // namespace grimlock::game
