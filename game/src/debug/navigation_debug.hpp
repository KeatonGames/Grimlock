#pragma once

#include "navigation/navigation_session.hpp"

#include <grimlock/grid_debug.hpp>

namespace grimlock::game {

class NavigationDebug final : public editor::GridDebug {
public:
    void menu(Engine& engine) override;
    void update(Engine& engine, double delta) override;

    void shutdown(Engine&) noexcept override {
        session_.shutdown();
    }

    [[nodiscard]] NavigationSession& session() noexcept {
        return session_;
    }

    [[nodiscard]] std::span<const std::array<glm::vec3, 3>> triangles() const noexcept override {
        return showNavMesh_ ? session_.navigation().debugTriangles() : std::span<const std::array<glm::vec3, 3>>{};
    }

private:
    void enemyInspector(EnemySystem& enemies);
    NavigationSession session_;
    std::optional<EnemyId> inspectedEnemy_;
    int count_ = 100;
    std::size_t lastSpawned_ = 0;
    std::string error_;
    bool showNavMesh_ = false;
};

} // namespace grimlock::game
