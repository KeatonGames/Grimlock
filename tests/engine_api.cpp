#include "grimlock/assets/animation.hpp"
#include "grimlock/engine.hpp"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace grimlock;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool smoke = argc > 1 && std::string_view{argv[1]} == "--smoke";
        const auto path = argc > 1 && !smoke ? std::filesystem::path{argv[1]}
                                             : std::filesystem::path{"assets/models/blackrat.glb"};
        Engine engine{EngineConfig{.windowTitle = "Engine public API checks"}};
        const auto model = engine.loadModel(path);
        const auto again = engine.loadModel(path.parent_path() / "." / path.filename());
        require(model == again, "canonical model paths did not reuse a resource");
        const auto info = engine.modelInfo(model);
        require(info.primitives == 3 && info.animationNames.size() == 13, "model metadata changed");
        const auto first = engine.createInstance(model), second = engine.createInstance(again);
        require(first != second, "instances are not independent");
        auto& a = engine.animation(first);
        auto& b = engine.animation(second);
        require(a.asset() == b.asset(), "instances duplicated the CPU resource");
        a.play(1);
        b.play(4);
        b.setSpeed(0.75F);
        a.seek(0.1F);
        a.pause();
        const auto dimensions = info.maximum - info.minimum;
        const auto normalization =
            glm::scale(glm::mat4{1},
                       glm::vec3{2 / std::max({dimensions.x, dimensions.y, dimensions.z})}) *
            glm::translate(glm::mat4{1}, -(info.minimum + dimensions * 0.5F));
        const Transform left{
            {-0.8F, 0, 0}, glm::angleAxis(0.2F, glm::vec3{0, 1, 0}), glm::vec3{0.8F}};
        engine.setTransform(first, left.matrix() * normalization);
        engine.setTransform(second,
                            glm::translate(glm::mat4{1}, glm::vec3{0.8F, 0, 0}) * normalization);
        require(engine.transform(first) != engine.transform(second), "transforms are shared");
        engine.setTint(first, glm::vec4{1});
        engine.setMaterialOverride(first, std::nullopt);
        bool rejected = false;
        try {
            engine.setTransform(InstanceId{}, glm::mat4{1});
        } catch (const std::out_of_range&) {
            rejected = true;
        }
        require(rejected, "invalid instance accepted");
        unsigned frames = 0;
        while (engine.update()) {
            ++frames;
            if (frames == 30) {
                require(std::abs(a.time() - 0.1F) < 1e-5F, "Engine advanced a paused instance");
                require(b.time() > 0, "Engine did not advance the other instance");
                require(a.model().animations[1].samplers[0].times ==
                            b.model().animations[1].samplers[0].times,
                        "playback modified immutable samples");
                a.resume();
            }
            if (!smoke && frames >= 120) {
                break;
            }
        }
        require(frames > 0, "no frames updated");
        std::cout << "PASS Engine API: resource reuse, metadata, independent transforms/playback, "
                     "automatic updates, invalid IDs and RAII shutdown.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Engine API: " << error.what() << '\n';
        return 1;
    }
}
