#include "platform/window.hpp"
#include "renderer/vulkan_renderer.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <glm/ext/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace grimlock;
using namespace grimlock::renderer;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

assets::ModelData makeModel(bool deformed) {
    assets::ModelData model;
    model.materials.resize(4);
    const std::array colors{glm::vec4{1, 0.1F, 0.1F, 1},
                            glm::vec4{0.1F, 1, 0.1F, 0.7F},
                            glm::vec4{0.1F, 0.1F, 1, 0.35F},
                            glm::vec4{1, 1, 0.1F, 0.55F}};
    for (std::size_t index = 0; index < model.materials.size(); ++index) {
        auto& material = model.materials[index];
        material.baseColor = colors[index];
        material.unlit = true;
        material.alphaMode = index == 0   ? assets::AlphaMode::opaque
                             : index == 1 ? assets::AlphaMode::mask
                                          : assets::AlphaMode::blend;
        material.doubleSided = index % 2 != 0;

        assets::MeshData mesh;
        mesh.material = index;
        mesh.vertices.resize(3);
        mesh.indices = {0, 1, 2};
        const float depth = static_cast<float>(index) * 0.15F;
        mesh.vertices[0].position = {-0.7F, -0.6F, depth};
        mesh.vertices[1].position = {0.7F, -0.6F, depth};
        mesh.vertices[2].position = {0, 0.7F, depth};
        for (std::size_t vertex = 0; vertex < mesh.vertices.size(); ++vertex) {
            mesh.vertices[vertex].normal = {0, 0, 1};
            mesh.vertices[vertex].tangent = {1, 0, 0, 1};
            mesh.vertices[vertex].weights0 = {1, 0, 0, 0};
            mesh.vertices[vertex].morphIndex = static_cast<std::uint32_t>(vertex);
        }
        if (deformed) {
            mesh.morphVertexCount = 3;
            mesh.morphTargetCount = 1;
            mesh.morphDeltas.resize(3);
            for (auto& delta : mesh.morphDeltas) {
                delta.position = {0.3F, 0.1F, 0, 0};
            }
        }
        model.meshes.push_back(std::move(mesh));
    }

    model.nodes.resize(3);
    for (auto& node : model.nodes) {
        node.matrixTransform = false;
    }
    model.nodes[0].meshes = {0, 1, 2, 3};
    model.nodes[0].children = {1, 2};
    model.nodes[1].parent = 0;
    model.nodes[2].parent = 0;
    model.nodes[2].meshes = {3};
    model.nodes[2].translation = {0.35F, 0.1F, -0.2F};
    model.nodes[2].localTransform = glm::translate(glm::mat4{1}, model.nodes[2].translation);
    model.roots = {0};
    if (deformed) {
        assets::SkinData skin;
        skin.joints = {1};
        skin.inverseBindMatrices = {glm::mat4{1}};
        model.skins.push_back(std::move(skin));
        model.nodes[0].skin = 0;
        model.nodes[0].weights = {0.25F};
        model.nodes[2].weights = {0.5F};
    }
    return model;
}

Camera testCamera() {
    Camera camera;
    camera.position = {0, 0, 6};
    return camera;
}

void renderFrame(VulkanRenderer& renderer, const Camera& camera, std::span<const ModelDraw> draws) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        SDL_PumpEvents();
        if (renderer.drawFrame(camera, draws)) {
            return;
        }
    }
    throw std::runtime_error("frame was not presented");
}

void checkValidFrames(SDL_Window* window) {
    VulkanRenderer renderer{window};
    const auto staticModel = renderer.uploadModel(makeModel(false));
    auto source = std::make_shared<assets::ModelData>(makeModel(true));
    const auto deformedModel = renderer.uploadModel(*source);
    assets::ModelInstance instance{source};
    auto firstPose = instance.pose();
    auto secondPose = firstPose;
    secondPose.weights[0] = {0.75F};
    secondPose.joints[0][0] = glm::translate(glm::mat4{1}, glm::vec3{0, 0.2F, 0});
    std::array draws{
        ModelDraw{.model = staticModel, .transform = glm::translate(glm::mat4{1}, glm::vec3{-0.4F, 0, -1})},
        ModelDraw{.model = deformedModel, .pose = &firstPose},
        ModelDraw{.model = deformedModel,
                  .transform = glm::scale(glm::mat4{1}, glm::vec3{-1, 1, 1}),
                  .pose = &secondPose,
                  .selected = true},
        ModelDraw{.model = staticModel, .baseColor = {0.5F, 1, 1, 1}, .materialOverride = 3},
        ModelDraw{.model = staticModel, .materialOverride = 2}};
    const auto camera = testCamera();
    renderFrame(renderer, camera, {});
    renderFrame(renderer, camera, draws);
    std::reverse(draws.begin(), draws.end());
    renderFrame(renderer, camera, draws);
    firstPose.world[0] = glm::translate(glm::mat4{1}, glm::vec3{0.2F, 0, -0.4F});
    renderFrame(renderer, camera, draws);
    for (auto& draw : draws) {
        draw.pose = nullptr;
    }
    renderFrame(renderer, camera, draws);
    require(SDL_SetWindowSize(window, 400, 300), SDL_GetError());
    SDL_PumpEvents();
    renderer.requestSwapchainRecreation();
    renderFrame(renderer, camera, draws);
    renderFrame(renderer, camera, {});
}

template <typename Edit> void rejects(SDL_Window* window, Edit edit, std::string_view expected) {
    // A rejected frame may already hold an acquired image; each case gets its own renderer.
    VulkanRenderer renderer{window};
    auto source = std::make_shared<assets::ModelData>(makeModel(true));
    const auto model = renderer.uploadModel(*source);
    assets::ModelInstance instance{source};
    auto pose = instance.pose();
    ModelDraw draw{.model = model, .pose = &pose};
    auto camera = testCamera();
    edit(draw, pose, camera);
    try {
        (void)renderer.drawFrame(camera, std::span{&draw, 1});
    } catch (const std::exception& error) {
        if (error.what() != expected) {
            throw std::runtime_error("Expected '" + std::string{expected} + "'; got: " + error.what());
        }
        return;
    }
    throw std::runtime_error("invalid draw was accepted");
}

void checkValidation(SDL_Window* window) {
    rejects(
        window,
        [](auto& draw, auto&, auto&) {
            draw.transform[0][0] = std::numeric_limits<float>::quiet_NaN();
        },
        "Non-finite model transform.");
    rejects(
        window,
        [](auto& draw, auto&, auto&) {
            draw.baseColor[0] = std::numeric_limits<float>::quiet_NaN();
            draw.transform[1][0] = std::numeric_limits<float>::quiet_NaN();
        },
        "Non-finite model color.");
    rejects(
        window,
        [](auto&, auto& pose, auto&) {
            pose.world.pop_back();
        },
        "Instance pose does not match the GPU model.");
    rejects(
        window,
        [](auto&, auto& pose, auto&) {
            pose.joints[0].clear();
        },
        "Instance joint palette does not match model skin.");
    rejects(
        window,
        [](auto&, auto& pose, auto&) {
            pose.world[0][0][0] = std::numeric_limits<float>::infinity();
        },
        "Non-finite combined node transform.");
    rejects(
        window,
        [](auto& draw, auto&, auto&) {
            draw.transform[0][0] = 0;
        },
        "Model/node transform must have a finite, invertible linear part.");
    rejects(
        window,
        [](auto& draw, auto&, auto& camera) {
            draw.transform[3] = glm::vec4{glm::vec3{std::numeric_limits<float>::max()}, 1};
            camera.position = {3, 2, 4};
        },
        "Non-finite primitive sort depth.");
    rejects(
        window,
        [](auto&, auto& pose, auto&) {
            pose.weights[0].clear();
        },
        "Deformation frame exceeds 4096 draws/65536 joint matrices/65536 morph weights, or morph count mismatch.");
}

} // namespace

int main() {
    try {
        platform::Window window{"Renderer frame checks", 320, 240};
        checkValidFrames(window.native());
        checkValidation(window.native());
        std::cout
            << "PASS renderer frames: materials, mirrored instances, poses, deformation, resize and validation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL renderer frames: " << error.what() << '\n';
        return 1;
    }
}
