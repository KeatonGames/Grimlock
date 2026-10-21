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

private:
    NavigationSession session_;
    int count_ = 100;
    std::size_t lastSpawned_ = 0;
    std::string error_;
};

} // namespace grimlock::game
