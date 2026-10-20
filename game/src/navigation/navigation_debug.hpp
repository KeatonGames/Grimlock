#pragma once

#include "navigation/navigation_demo.hpp"

#include <grimlock/grid_debug.hpp>

namespace grimlock::game {

class NavigationDebug final : public editor::GridDebug {
public:
    void menu(Engine& engine) override;
    editor::GridDebugView view(Engine& engine) override;
    void update(Engine& engine, double delta) override;

    [[nodiscard]] NavigationDemo& demo() noexcept {
        return demo_;
    }

private:
    NavigationDemo demo_;
    bool showDirections_ = true, showCosts_ = false;
};

} // namespace grimlock::game
