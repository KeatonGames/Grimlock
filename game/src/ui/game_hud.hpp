#pragma once

#include "input/ground_command.hpp"
#include "runtime/level_catalog.hpp"
#include "runtime/level_runtime.hpp"
#include "ui/hero_route_visual.hpp"

#include <grimlock/imgui_overlay.hpp>

namespace grimlock::game {

class GameHud final : public ui::ImGuiOverlay {
public:
    GameHud(Engine& engine,
            LevelRuntime& runtime,
            bool startInMenu = false,
            const std::filesystem::path& initialLevel = {},
            const std::filesystem::path& optionsFile = {});
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
    void configureUi() override;
    void releaseUi() noexcept override;
    void targetChanged(const OverlayTarget& target) override;
    void initializeMenu();
    ImTextureRef menuImage(const std::filesystem::path& path);
    void menuBackground(bool darkened);
    void saveOptions();
    void applyOptions();
    void transitions();
    void menu();
    void refreshLevels();
    void clearFeedback();
    void progressNotice();
    void status();
    void bossHealth();
    void construction();
    void coins();
    void coinIcon(ImVec2 center, float size);
    void towerInfo();
    void result();
    void placementOverlay(const Camera& camera);
    void updateTowerPreview();
    void clearTowerPreview();
    void updateSelectionMarker();
    void clearSelectionMarker();
    void updateHeroPathPreview(const std::optional<Ray>& ray);
    void heroMovementOverlay(const Camera& camera);
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
    std::optional<ObjectId> hoveredObject_;
    std::optional<InstanceId> selectionMarker_;
    std::optional<InstanceId> towerPreview_, towerDefenderPreview_;
    std::string towerPreviewType_;
    glm::vec3 towerDefenderFoot_{0};
    float towerDefenderScale_ = 1;
    std::optional<glm::dvec3> orderPosition_;
    HeroId orderHero_ = 0, previewHero_ = 0;
    NavigationPath hoverPath_;
    HeroRouteVisual hoverRoute_;
    std::map<HeroId, HeroRouteVisual> orderedRoutes_;
    std::optional<glm::dvec3> hoverDestination_;
    glm::dvec3 hoverOrigin_{0};
    double hoverQueryTime_ = 0;
    float orderRemaining_ = 0;
    bool orderAttack_ = false, cancelWorldInput_ = false;

    struct BossBar {
        EnemyId enemy;
        float fraction = 1;
    };

    std::vector<BossBar> bossBars_;
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
        float scale = 1, gap = 14, top = 62;
        float dockWidth = 420, dockHeight = 124, contextWidth = 320, contextHeight = 270;
    } layout_;

    float uiScale_ = 1, entrance_ = 0;
    bool settingsOpen_ = false, worldHealthBars_ = true;
    std::optional<LevelPhase> displayedPhase_;
    std::size_t displayedWave_ = 0;
    std::string notification_;
    float notificationRemaining_ = 0;
    bool modalFrame_ = false;
    std::vector<std::array<glm::vec3, 3>> coinTriangles_;
    glm::vec3 coinCenter_{0};
    float coinHeight_ = 1;
    std::map<std::filesystem::path, std::unique_ptr<ImTextureData>> menuImages_;
    ImFont* menuFont_ = nullptr;
    std::filesystem::path optionsPath_;
    int resolution_ = 1;
    std::uint32_t msaaSamples_ = 4;
    bool fullscreen_ = false, vsync_ = true;
    std::string optionsError_;
};

} // namespace grimlock::game
