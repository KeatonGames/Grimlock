#include "game.hpp"

#include "grimlock/scene.hpp"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace grimlock::game {

Game::Game(const std::filesystem::path& path, const std::filesystem::path& environmentFolder)
    : engine_(EngineConfig{.environmentFolder = environmentFolder}) {
    if (path.empty() || path.extension() == ".json") {
        engine_.setScene(engine_.loadScene(path.empty() ? "assets/scenes/demo.json" : path));
        engine_.setCameraNavigationEnabled();
        engine_.setObjectSelectionEnabled();
        if (engine_.scene()->navigationGrid()) {
            (void)navigation_.spawn(engine_);
        }

        return;
    }

    auto& lighting = engine_.lighting();
    lighting.points[0] = {{-2, 2, 2}, 28, {1, 0.8F, 0.65F}, 8};
    lighting.points[1] = {{2, 1.5F, 1}, 16, {0.6F, 0.75F, 1}, 7};
    lighting.pointCount = 2;

    (void)loadExample(path);
    engine_.setCameraNavigationEnabled();
}

InstanceId Game::loadExample(const std::filesystem::path& path) {
    const auto model = engine_.loadModel(path);
    const auto info = engine_.modelInfo(model);
    const auto dimensions = info.maximum - info.minimum;
    const float largest = std::max({dimensions.x, dimensions.y, dimensions.z});

    if (!std::isfinite(largest) || largest <= 1e-8F) {
        throw std::runtime_error("Model has invalid framing bounds: " + path.string());
    }

    const auto normalization = glm::scale(glm::mat4{1}, glm::vec3{2 / largest}) *
                               glm::translate(glm::mat4{1}, -(info.minimum + dimensions * 0.5F));
    const auto instance = engine_.createInstance(model);
    engine_.setTransform(instance, normalization);

    if (!info.animationNames.empty()) {
        engine_.animation(instance).play(0);

        for (std::size_t clip = 0; clip < info.animationNames.size(); ++clip) {
            std::cout << "  Animation " << clip << ": " << info.animationNames[clip] << '\n';
        }
    }

    return instance;
}

void Game::run() {
    while (engine_.update([&](double delta) {
        navigation_.update(engine_, delta);
    })) {
    }
}

} // namespace grimlock::game
