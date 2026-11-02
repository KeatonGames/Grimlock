#include "game.hpp"

namespace grimlock::game {
Game::Game(const std::filesystem::path& level,
           const std::filesystem::path& environment,
           const std::filesystem::path& assets)
    : engine_(EngineConfig{.windowTitle = "Grimlock Game",
                           .environmentFolder = environment,
                           .assetRoot = assets.empty() ? assetRootForLevel(level) : assets}) {
    hud_ = std::make_unique<GameHud>(engine_, runtime_, true, level);
}

void Game::run() {
    while (!hud_->quitRequested() && engine_.update([&](double delta) {
        runtime_.update(engine_, delta);
    })) {
    }
}
} // namespace grimlock::game
