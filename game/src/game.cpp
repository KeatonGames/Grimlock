#include "game.hpp"

namespace grimlock::game {
Game::Game(const std::filesystem::path& level, const std::filesystem::path& environment)
    : engine_(EngineConfig{.windowTitle = "Grimlock Game",
                           .environmentFolder = environment,
                           .assetRoot = assetRootForLevel(level)}) {
    runtime_.load(engine_, level.empty() ? std::filesystem::path{"assets/levels/first_watch.json"} : level);
    hud_ = std::make_unique<GameHud>(engine_, runtime_);
}

void Game::run() {
    while (engine_.update([&](double delta) {
        runtime_.update(engine_, delta);
    })) {
    }
}
} // namespace grimlock::game
