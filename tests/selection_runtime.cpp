#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <grimlock/engine.hpp>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace grimlock;

namespace {

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void push(SDL_Event event) {
    require(SDL_PushEvent(&event), SDL_GetError());
}

glm::vec2 project(const Camera& camera, glm::vec3 point, SDL_Window* window) {
    int width = 0, height = 0, pixelWidth = 0, pixelHeight = 0;
    SDL_GetWindowSize(window, &width, &height);
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    auto clip = camera.projection(static_cast<float>(pixelWidth) / pixelHeight) * camera.view() * glm::vec4{point, 1};
    clip /= clip.w;

    return (glm::vec2{clip} + 1.0F) * 0.5F * glm::vec2{width, height};
}

void button(SDL_WindowID id, Uint8 button, bool down, glm::vec2 position) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = id;
    event.button.button = button;
    event.button.x = position.x;
    event.button.y = position.y;
    push(event);
}

void click(Engine& engine, SDL_WindowID id, glm::vec2 position) {
    button(id, SDL_BUTTON_LEFT, true, position);
    require(engine.update(), "click update closed");

    button(id, SDL_BUTTON_LEFT, false, position);
    require(engine.update(), "release update closed");
}

void clickModel(Engine& engine, SDL_Window* window, ObjectId object) {
    auto& instance = engine.scene()->animation(object);
    const auto& model = instance.model();
    const auto& pose = instance.pose();
    const auto sceneWorld = engine.scene()->worldTransform(object);
    const auto id = SDL_GetWindowID(window);
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);

    for (const auto node : pose.activeNodes) {
        for (const auto meshIndex : model.nodes[node].meshes) {
            const auto& mesh = model.meshes[meshIndex];
            const auto stride = std::max<std::size_t>(3, mesh.indices.size() / 60 / 3 * 3);

            for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += stride) {
                glm::vec3 center{0};

                for (std::size_t corner = 0; corner < 3; ++corner) {
                    const auto& vertex = mesh.vertices[mesh.indices[index + corner]];
                    auto position = vertex.position;

                    for (std::uint32_t target = 0; target < mesh.morphTargetCount; ++target) {
                        position +=
                            glm::vec3{mesh.morphDeltas[static_cast<std::size_t>(target) * mesh.morphVertexCount +
                                                       vertex.morphIndex]
                                          .position} *
                            pose.weights[node][target];
                    }

                    if (!pose.joints[node].empty()) {
                        glm::mat4 skin{0};

                        for (int lane = 0; lane < 4; ++lane) {
                            skin += pose.joints[node][vertex.joints0[lane]] * vertex.weights0[lane];
                            skin += pose.joints[node][vertex.joints1[lane]] * vertex.weights1[lane];
                        }

                        position = glm::vec3{skin * glm::vec4{position, 1}};
                    }

                    center += glm::vec3{sceneWorld * pose.world[node] * glm::vec4{position, 1}} / 3.0F;
                }

                const auto cursor = project(engine.scene()->resolvedCamera(), center, window);

                if (cursor.x < 1 || cursor.y < 1 || cursor.x >= width - 1 || cursor.y >= height - 1) {
                    continue;
                }

                click(engine, id, cursor);

                if (engine.selectedObject() == object) {
                    return;
                }
            }
        }
    }

    throw std::runtime_error("Could not click visible GLB scene object " + std::to_string(object));
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected Game source and output paths");

        const std::filesystem::path game{argv[1]}, output{argv[2]};
        Engine engine{EngineConfig{.windowTitle = "Selection runtime checks",
                                   .width = 800,
                                   .height = 600,
                                   .assetRoot = game / "assets"}};
        auto candidate = std::make_unique<Scene>();
        const auto a = candidate->createObject("left"), b = candidate->createObject("right");

        for (auto object : {a, b}) {
            candidate->setModel(object, "models/cube.obj");
        }

        candidate->setTransform(a, Transform{{-1.2F, 0, 0}});
        candidate->setTransform(b, Transform{{1.2F, 0, 0}});
        Camera camera;
        camera.position = {0, 2, 7};
        camera.target = {0, 0, 0};
        candidate->setCamera("main", SceneCamera{camera});
        candidate->setNavigationGrid(GridConfiguration{8, 8, 1, {-4, -1, -4}});
        candidate->navigationGrid()->setWalkable({2, 5}, false);
        candidate->navigationGrid()->setWalkable({6, 5}, false);
        engine.setScene(std::move(candidate));
        engine.setCameraNavigationEnabled();
        engine.setObjectSelectionEnabled();
        require(engine.update(), "initial update closed");

        int count = 0;
        auto windows = SDL_GetWindows(&count);
        require(windows && count == 1, "expected Engine window");

        auto* window = windows[0];
        SDL_free(windows);
        const auto id = SDL_GetWindowID(window);
        SDL_Event focus{};
        focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        focus.window.windowID = id;
        push(focus);
        require(engine.update(), "focus update closed");

        auto left = project(engine.scene()->resolvedCamera(), {-1.2F, 0, 0}, window),
             right = project(engine.scene()->resolvedCamera(), {1.2F, 0, 0}, window);
        click(engine, id, left);
        require(engine.selectedObject() == a, "left object was not picked");

        const auto view = engine.scene()->resolvedCamera();
        click(engine, id, right);
        require(engine.selectedObject() == b, "shared model instance did not transfer selection");
        require(engine.scene()->resolvedCamera().position == view.position, "left click moved camera");

        click(engine, id, project(engine.scene()->resolvedCamera(), {0, -1, 2.5F}, window));
        require(!engine.selectedObject(), "generated floor intercepted object selection");
        click(engine, id, project(engine.scene()->resolvedCamera(), {2.5F, 0.5F, 1.5F}, window));
        require(!engine.selectedObject(), "generated wall became selectable");

        button(id, SDL_BUTTON_LEFT, true, left);
        require(engine.update(), "held setup closed");
        require(engine.selectedObject() == a, "press did not select");

        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = id;
        motion.motion.x = right.x;
        motion.motion.y = right.y;
        push(motion);
        require(engine.update(), "held update closed");
        require(engine.selectedObject() == a, "held mouse repeatedly picked");

        button(id, SDL_BUTTON_LEFT, false, right);
        require(engine.update(), "held release closed");

        click(engine, id, {5, 5});
        require(!engine.selectedObject(), "empty click did not deselect");

        engine.scene()->setSelectable(a, false);
        click(engine, id, left);
        require(!engine.selectedObject(), "non-selectable picked");

        engine.scene()->setSelectable(a, true);
        engine.scene()->setEnabled(a, false);
        click(engine, id, left);
        require(!engine.selectedObject(), "disabled picked");

        engine.scene()->setEnabled(a, true);
        click(engine, id, right);
        button(id, SDL_BUTTON_RIGHT, true, right);
        button(id, SDL_BUTTON_LEFT, true, left);
        require(engine.update(), "navigation conflict closed");
        require(engine.selectedObject() == b, "pan/selection buttons conflicted");

        motion.motion.xrel = 12;
        motion.motion.yrel = -5;
        push(motion);
        require(engine.update(), "pan closed");
        require(engine.selectedObject() == b, "pan cleared selection");

        button(id, SDL_BUTTON_LEFT, false, left);
        button(id, SDL_BUTTON_RIGHT, false, right);
        require(engine.update(), "pan release closed");
        require(SDL_SetWindowSize(window, 1100, 650), SDL_GetError());
        require(engine.update(), "resize closed");

        right = project(engine.scene()->resolvedCamera(), {1.2F, 0, 0}, window);
        engine.clearSelection();
        click(engine, id, right);
        require(engine.selectedObject() == b, "resize broke picking");

        button(id, SDL_BUTTON_MIDDLE, true, right);
        require(engine.update(), "orbit start closed");

        motion.motion.xrel = 35;
        motion.motion.yrel = 12;
        push(motion);
        require(engine.update(), "orbit closed");

        button(id, SDL_BUTTON_MIDDLE, false, right);
        require(engine.update(), "orbit release closed");

        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.windowID = id;
        wheel.wheel.y = 1;
        push(wheel);
        require(engine.update(), "zoom closed");
        require(engine.selectedObject() == b, "orbit/zoom cleared selection");

        right = project(engine.scene()->resolvedCamera(), {1.2F, 0, 0}, window);
        engine.clearSelection();
        click(engine, id, right);
        require(engine.selectedObject() == b, "orbit/zoom broke picking");

        const glm::vec3 leftCenter{-1.2F, 0, 0};
        const auto towardCamera = glm::normalize(engine.scene()->resolvedCamera().position - leftCenter);
        engine.scene()->setTransform(b, Transform{leftCenter + towardCamera * 1.5F});
        require(engine.update(), "overlap setup closed");

        click(engine, id, project(engine.scene()->resolvedCamera(), leftCenter, window));
        require(engine.selectedObject() == b, "closest object did not win over earlier map entry");

        engine.saveScene(output);
        auto saved = Scene::load(output);
        require(!saved->selectedObject() && saved->object(a).selectable, "selection leaked into serialization");

        engine.scene()->removeObject(b);
        require(!engine.selectedObject(), "deletion left stale selection");

        engine.setObjectSelectionEnabled(false);
        require(!engine.objectSelectionEnabled(), "selection disable failed");

        engine.unloadScene();
        require(!engine.selectedObject(), "unload retained selection");

        engine.setScene(engine.loadScene(game / "assets/scenes/demo.json"));
        engine.setObjectSelectionEnabled();
        require(engine.update(), "default scene update closed");
        require(engine.scene()->animation(1).model().meshes.size() > 1, "assassin multi-mesh model missing");

        const auto before = engine.scene()->animation(6).time();

        for (int frame = 0; frame < 5; ++frame) {
            require(engine.update(), "animation update closed");
        }

        require(engine.scene()->animation(6).time() > before, "existing scene animation stopped");

        // Freeze only test playback so projected triangle centers stay deterministic.
        for (const auto& [object, value] : engine.scene()->objects()) {
            if (value.model) {
                engine.scene()->animation(object).pause();
            }
        }

        for (auto object : {ObjectId{1}, ObjectId{5}, ObjectId{6}}) {
            clickModel(engine, window, object);
            require(engine.selectedObject() == object, "GLB selection lost");
        }

        engine.clearSelection();
        require(engine.update(), "clear highlight closed");

        engine.unloadScene();
        std::cout << "PASS selection runtime: clicks/hold/empty, shared instances, disabled policy, pan, "
                     "resize, serialization, deletion/unload, assassin and animated GLB highlighting.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL selection runtime: " << error.what() << '\n';

        return 1;
    }
}
