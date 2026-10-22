#pragma once

#include "navigation/navigation_session.hpp"
#include "systems/level_session.hpp"

#include <grimlock/grid_debug.hpp>

namespace grimlock::game {

class NavigationDebug final : public editor::GridDebug {
public:
    void menu(Engine& engine) override;
    void update(Engine& engine, double delta) override;
    void panel(Engine& engine) override;
    [[nodiscard]] std::optional<std::filesystem::path> takeSceneRequest() override;
    void sceneOpened(Engine& engine, const std::filesystem::path& path) override;
    bool viewport(Engine& engine, const editor::ViewportInput& input) override;

    [[nodiscard]] bool capturesSelection() const noexcept override {
        return placingTower_;
    }

    [[nodiscard]] std::span<const editor::DebugBox> boxes() const noexcept override {
        return showPreview_ ? std::span<const editor::DebugBox>{preview_} : std::span<const editor::DebugBox>{};
    }

    [[nodiscard]] std::span<const editor::DebugQuad> quads() const noexcept override {
        return zoneQuads_;
    }

    void shutdown(Engine&) noexcept override {
        placingTower_ = showPreview_ = false;
        level_.stop(session_);
        session_.shutdown();
    }

    [[nodiscard]] NavigationSession& session() noexcept {
        return session_;
    }

    [[nodiscard]] LevelSession& level() noexcept {
        return level_;
    }

    [[nodiscard]] std::span<const std::array<glm::vec3, 3>> triangles() const noexcept override {
        return showNavMesh_ ? session_.navigation().debugTriangles() : std::span<const std::array<glm::vec3, 3>>{};
    }

private:
    void enemyInspector(EnemySystem& enemies);
    void towerMenu(Engine& engine);
    void buildZoneMenu(Engine& engine);
    void levelMenu(Engine& engine);
    void requestLevel(Engine& engine, LevelDefinition definition);
    LevelSession level_;
    std::optional<LevelDefinition> pendingLevel_;
    std::optional<std::filesystem::path> requestedScene_;
    NavigationSession session_;
    std::optional<EnemyId> inspectedEnemy_;
    int count_ = 100;
    std::size_t lastSpawned_ = 0;
    std::string error_;
    bool showNavMesh_ = false;
    bool placingTower_ = false, showPreview_ = false, sceneChanged_ = false;
    const Scene* placementScene_ = nullptr;
    std::string towerType_ = "basic";
    float towerYaw_ = 0;
    bool showZones_ = true;
    std::vector<editor::DebugQuad> zoneQuads_;

    struct PreviewKey {
        Tower tower;
        SpawnArea area;
        std::optional<glm::dvec2> destination;
        std::uint64_t revision;
        EnemyTypeId type;
        bool operator==(const PreviewKey&) const = default;
    };

    std::optional<PreviewKey> previewKey_;
    std::string previewError_, rejectedPlacement_;
    std::array<editor::DebugBox, 1> preview_;
};

} // namespace grimlock::game
