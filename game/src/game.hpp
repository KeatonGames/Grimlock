#pragma once

#include "grimlock/engine.hpp"
#include "runtime/level_runtime.hpp"
#include "ui/game_hud.hpp"

namespace grimlock::game {

class Game final {
public:
    explicit Game(const std::filesystem::path& level = {}, const std::filesystem::path& environment = {});
    void run();

private:
    Engine engine_;
    LevelRuntime runtime_;
    std::unique_ptr<GameHud> hud_;
};

} // namespace grimlock::game
