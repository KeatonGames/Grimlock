#include <cmath>
#include <grimlock/orbit_camera.hpp>
#include <grimlock/scene.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace grimlock;

namespace {
void require(bool value, const char* text) {
    if (!value) {
        throw std::runtime_error(text);
    }
}

bool near(glm::vec3 a, glm::vec3 b, float tolerance = 1e-4F) {
    return glm::length(a - b) < tolerance;
}

float distance(const Camera& c) {
    return glm::length(c.position - c.target);
}

template <class F> void rejects(F&& action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "invalid camera configuration accepted");
}
} // namespace

int main() {
    try {
        Camera camera;
        camera.position = {3.5F, 2.2F, 7.5F};
        camera.target = {0, -0.4F, 0};
        const Camera initial = camera;
        OrbitCameraController controller;
        controller.synchronize(camera);
        require(camera.position == initial.position && camera.target == initial.target,
                "initial camera jumped");
        require(near(controller.config().target, initial.target), "initial target ignored");
        InputState input;
        input.focused = true;
        input.mouseMotion = {400, 300};
        input.mouseButtons[0].held = true;
        input.keys[26].held = true;
        require(!controller.update(camera, input, 720) && camera.position == initial.position,
                "left/key/no-drag motion moved camera");
        input.mouseButtons[0].held = false;
        input.mouseButtons[2].held = true;
        require(!controller.update(camera, input, 720) && camera.position == initial.position,
                "drag start consumed stale motion");
        input.mouseMotion = {25, -12};
        const auto offset = camera.position - camera.target;
        const auto radius = distance(camera);
        require(controller.update(camera, input, 720), "pan ignored");
        require(near(camera.position - camera.target, offset) &&
                    std::abs(distance(camera) - radius) < 1e-4F,
                "pan changed orientation/distance");
        require(camera.position.y == initial.position.y && camera.target.y == initial.target.y,
                "pan left ground plane");
        const auto right =
            glm::normalize(glm::cross(glm::normalize(offset * -1.0F), glm::vec3{0, 1, 0}));
        require(glm::dot(camera.target - initial.target, right) < 0,
                "right drag did not pull world naturally");
        input.mouseButtons[2].pressed = input.mouseButtons[2].released = true;
        input.mouseMotion = {500, 500};
        const auto restarted = camera;
        require(!controller.update(camera, input, 720) && camera.position == restarted.position,
                "same-frame release/repress reused motion");
        input.mouseButtons[2].pressed = input.mouseButtons[2].released = false;
        input.mouseButtons[2].held = false;
        input.mouseMotion = {500, 500};
        const auto panEnd = camera;
        require(!controller.update(camera, input, 720) && camera.position == panEnd.position,
                "drag release jumped");
        input.mouseButtons[1].held = true;
        require(!controller.update(camera, input, 720), "orbit start consumed stale motion");
        input.mouseMotion = {30, 20};
        const auto target = camera.target;
        require(controller.update(camera, input, 720) && near(camera.target, target),
                "orbit moved target");
        require(std::abs(distance(camera) - radius) < 1e-4F && camera.up == glm::vec3{0, 1, 0},
                "orbit distance/world-up drift");
        for (int i = 0; i < 10000; ++i) {
            input.mouseMotion = {17, i % 2 ? 10.0F : -10.0F};
            (void)controller.update(camera, input, 720);
        }
        require(std::isfinite(controller.config().yaw) &&
                    std::abs(controller.config().yaw) <= 3.141593F,
                "yaw was not wrapped");
        require(std::abs(distance(camera) - radius) < 1e-4F,
                "long orbit accumulated distance drift");
        input.mouseMotion = {0, 1e8F};
        (void)controller.update(camera, input, 720);
        require(controller.config().pitch == controller.config().maxPitch,
                "upper pitch limit failed");
        input.mouseMotion = {0, -1e8F};
        (void)controller.update(camera, input, 720);
        require(controller.config().pitch >= controller.config().minPitch && camera.position.y >= 0,
                "ground/pitch floor failed");
        input.mouseButtons[1].held = false;
        input.mouseMotion = {0, 0};
        input.mouseWheel.y = 1;
        const auto direction = glm::normalize(camera.position - camera.target);
        const auto fov = camera.verticalFovDegrees;
        (void)controller.update(camera, input, 720);
        require(distance(camera) < radius &&
                    near(glm::normalize(camera.position - camera.target), direction),
                "wheel changed orientation or did not zoom in");
        require(camera.target == target && camera.verticalFovDegrees == fov &&
                    controller.config().panSensitivity == 1,
                "zoom changed target/FOV/sensitivity");
        input.mouseWheel.y = 1e20F;
        (void)controller.update(camera, input, 720);
        require(camera.position.y >= 0, "zoom went below world ground");
        input.mouseWheel.y = -1e20F;
        (void)controller.update(camera, input, 720);
        require(std::abs(distance(camera) - controller.config().maxDistance) < 1e-3F,
                "maximum zoom bound failed");
        OrbitCameraConfig config;
        config.target = {0, 0, 0};
        config.distance = 10;
        controller.configure(config);
        controller.apply(camera);
        input.mouseWheel.y = 1e20F;
        (void)controller.update(camera, input, 720);
        require(std::abs(distance(camera) - config.minDistance) < 1e-5F,
                "minimum zoom bound failed");
        input.focused = false;
        input.mouseButtons[2].held = true;
        input.mouseMotion = {100, 100};
        input.mouseWheel.y = 5;
        const auto unfocused = camera;
        require(!controller.update(camera, input, 720) && camera.position == unfocused.position,
                "unfocused input moved camera");
        input.focused = true;
        input.mouseWheel.y = 0;
        require(!controller.update(camera, input, 720), "focus regain reused old drag");
        rejects([&] {
            auto c = config;
            c.minDistance = 0;
            controller.configure(c);
        });
        rejects([&] {
            auto c = config;
            c.maxPitch = 1.570796327F;
            controller.configure(c);
        });
        rejects([&] {
            auto c = config;
            c.panSensitivity = std::numeric_limits<float>::quiet_NaN();
            controller.configure(c);
        });
        rejects([&] {
            auto c = config;
            c.target.y = -1000;
            controller.configure(c);
        });
        Scene scene;
        const auto parent = scene.createObject();
        Transform transform{{1, 2, 3}, glm::angleAxis(0.4F, glm::vec3{0, 1, 0}), {2, 1.5F, 0.7F}};
        scene.setTransform(parent, transform);
        SceneCamera attached;
        attached.object = parent;
        scene.setCamera("attached", attached);
        scene.setActiveCamera("attached");
        auto world = scene.resolvedCamera();
        controller.synchronize(world);
        input = {};
        input.focused = true;
        input.mouseWheel.y = 1;
        (void)controller.update(world, input, 720);
        scene.setResolvedCamera(world);
        require(scene.cameras().at("attached").object == parent &&
                    near(scene.resolvedCamera().position, world.position),
                "attached camera world/local conversion failed");
        std::cout << "PASS camera: initial pose, unmapped input, ground pan, stable orbit, "
                     "pitch/zoom bounds, focus, attached-camera conversion.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL camera: " << e.what() << '\n';
        return 1;
    }
}
