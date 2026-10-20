#include "grimlock/assets/animation.hpp"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

using namespace grimlock::assets;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool near(float actual, float expected) {
    return std::isfinite(actual) && std::abs(actual - expected) < 1e-5F;
}

void requireMatrix(const glm::mat4& actual, const glm::mat4& expected, const char* message) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            require(near(actual[column][row], expected[column][row]), message);
        }
    }
}

std::shared_ptr<ModelData> makeModel() {
    auto model = std::make_shared<ModelData>();
    model->nodes.resize(1);
    model->nodes[0].matrixTransform = false;
    model->nodes[0].name = "animated";
    model->roots = {0};
    model->animations.resize(1);
    model->animations[0].end = 7;
    return model;
}

void addChannel(AnimationData& clip, std::size_t node, AnimationPath path, AnimationSampler sampler) {
    clip.channels.push_back({clip.samplers.size(), node, path});
    clip.samplers.push_back(std::move(sampler));
}

void samplingPaths() {
    auto model = makeModel();
    model->nodes[0].weights = {0.25F, 0.75F};
    model->animations.resize(2);
    auto& clip = model->animations[0];
    addChannel(clip, 0, AnimationPath::translation, {{1, 3, 6}, {0, 0, 0, 4, 2, -2, 10, 8, 4}, 3});
    addChannel(clip, 0, AnimationPath::scale, {{1, 3}, {1, 2, 3, 2, 3, 4}, 3, Interpolation::step});
    addChannel(clip,
               0,
               AnimationPath::weights,
               {{1, 3}, {0, 0, 0, 1, 1, -1, -1, 1, 4, 3, 0, 0}, 2, Interpolation::cubicSpline});
    const float halfSqrt = std::sqrt(0.5F);
    addChannel(clip, 0, AnimationPath::rotation, {{1, 3}, {0, 0, 0, 1, 0, 0, -halfSqrt, -halfSqrt}, 4});

    const auto originalValues = clip.samplers[2].values;
    ModelInstance instance{model};
    ModelInstance independent{model};
    instance.play();
    requireMatrix(instance.pose().local[0], glm::scale(glm::mat4{1}, {1, 2, 3}), "first-key clamping");

    instance.seek(2);
    const auto expected = glm::translate(glm::mat4{1}, {2, 1, -1}) *
                          glm::rotate(glm::mat4{1}, glm::radians(45.F), {0, 0, 1}) *
                          glm::scale(glm::mat4{1}, {1, 2, 3});
    requireMatrix(instance.pose().local[0], expected, "linear TRS / shortest-path quaternion slerp");
    require(near(instance.pose().weights[0][0], 2.5F) && near(instance.pose().weights[0][1], 1.5F),
            "cubic morph tangents must be scaled by the key interval");

    instance.seek(3);
    require(near(glm::length(glm::vec3{instance.pose().local[0][0]}), 2), "step at exact key");
    instance.seek(4.5F);
    require(near(instance.pose().world[0][3].x, 7) && near(instance.pose().world[0][3].y, 5),
            "second nonuniform key interval");
    instance.seek(7);
    require(near(instance.pose().world[0][3].x, 10) && near(instance.pose().weights[0][0], 4), "last-key clamping");
    require(clip.samplers[2].values == originalValues && independent.restPose() &&
                independent.pose().weights[0] == model->nodes[0].weights,
            "sampling changed shared asset data or another instance");

    instance.stop(false);
    require(!instance.running() && !instance.restPose() && near(instance.pose().world[0][3].x, 10),
            "stop without restoring the rest pose");
    instance.play(1);
    requireMatrix(instance.pose().local[0], glm::mat4{1}, "clip switch must reset unanimated TRS");
    require(instance.pose().weights[0] == model->nodes[0].weights, "clip switch must reset morph defaults");
    instance.stop();
    require(instance.restPose(), "rest-pose status after stop");
}

void quaternionModesAndSingleKeys() {
    auto model = makeModel();
    auto& clip = model->animations[0];
    addChannel(clip,
               0,
               AnimationPath::rotation,
               {{1, 3},
                {0, 0, 0, 0,  // First key: incoming tangent.
                 0, 0, 0, 1,  // First key: value (identity).
                 0, 0, 1, 0,  // First key: outgoing tangent.
                 1, 0, 0, 0,  // Second key: incoming tangent.
                 0, 0, 1, 0,  // Second key: value (180 degrees about Z).
                 0, 0, 0, 0}, // Second key: outgoing tangent.
                4,
                Interpolation::cubicSpline});
    ModelInstance cubic{model};
    cubic.play();
    cubic.seek(2);
    const glm::quat expected = glm::normalize(glm::quat{0.5F, -0.25F, 0, 0.75F});
    requireMatrix(cubic.pose().local[0], glm::mat4_cast(expected), "cubic quaternion tangents / normalization");

    auto stepModel = makeModel();
    addChannel(stepModel->animations[0],
               0,
               AnimationPath::rotation,
               {{1, 3}, {0, 0, 0, 2, 0, 0, 2, 0}, 4, Interpolation::step});
    ModelInstance step{stepModel};
    step.play();
    step.seek(2);
    requireMatrix(step.pose().local[0], glm::mat4{1}, "step quaternion normalization");
    step.seek(3);
    requireMatrix(step.pose().local[0],
                  glm::rotate(glm::mat4{1}, glm::radians(180.F), {0, 0, 1}),
                  "step quaternion at exact key");

    for (const auto interpolation : {Interpolation::step, Interpolation::linear, Interpolation::cubicSpline}) {
        auto singleModel = makeModel();
        AnimationSampler sampler{{2}, {3, 4, 5}, 3, interpolation};

        if (interpolation == Interpolation::cubicSpline) {
            sampler.values = {100, 200, 300, 3, 4, 5, -100, -200, -300};
        }

        addChannel(singleModel->animations[0], 0, AnimationPath::translation, std::move(sampler));
        ModelInstance single{singleModel};
        single.play();

        for (const float time : {0.F, 2.F, 7.F}) {
            single.seek(time);
            requireMatrix(single.pose().local[0],
                          glm::translate(glm::mat4{1}, {3, 4, 5}),
                          "single-key sampler must remain constant");
        }
    }
}

void hierarchyAndSkinning() {
    auto model = makeModel();
    model->nodes.resize(6);
    auto& nodes = model->nodes;
    nodes[0].matrixTransform = true;
    nodes[0].localTransform = glm::translate(glm::mat4{1}, {10, 0, 0}) * glm::scale(glm::mat4{1}, {2, 3, 4});
    nodes[0].children = {1, 2, 3};

    for (const std::size_t child : {1, 2, 3}) {
        nodes[child].parent = 0;
        nodes[child].matrixTransform = false;
    }

    nodes[1].translation = {1, 0, 0};
    nodes[1].scale = {-1, 2, 1};
    nodes[1].skin = 0;
    nodes[1].weights = {0.5F};
    nodes[2].translation = {0, 2, 0};
    nodes[2].children = {4};
    nodes[3].translation = {0, 0, 3};
    nodes[3].skin = 0;
    nodes[3].bindPosePreview = true;
    nodes[4].parent = 2;
    nodes[4].matrixTransform = false;
    nodes[4].translation = {0, 0, 1};
    nodes[5].matrixTransform = false;
    model->skins.push_back(
        {"skin", {4, 2}, {glm::translate(glm::mat4{1}, {0, -1, 0}), glm::scale(glm::mat4{1}, {1, 2, 1})}});
    auto& clip = model->animations[0];
    addChannel(clip, 2, AnimationPath::translation, {{0, 1}, {0, 2, 0, 0, 4, 0}, 3});
    addChannel(clip, 5, AnimationPath::translation, {{0, 1}, {0, 0, 0, 7, 8, 9}, 3});
    addChannel(clip, 0, AnimationPath::translation, {{0}, {100, 100, 100}, 3});
    addChannel(clip, 1, AnimationPath::weights, {{0, 1}, {0.5F, 1}, 1});
    ModelInstance instance{model};
    instance.play();
    instance.seek(1);
    const auto& pose = instance.pose();
    require(pose.activeNodes == std::vector<std::size_t>{0, 1, 2, 3, 4}, "selected-scene membership/order");
    requireMatrix(pose.local[0], nodes[0].localTransform, "explicit matrix must override sampled TRS");
    requireMatrix(pose.world[4],
                  glm::translate(glm::mat4{1}, {10, 12, 4}) * glm::scale(glm::mat4{1}, {2, 3, 4}),
                  "parent-first world transforms");
    requireMatrix(pose.world[5], glm::translate(glm::mat4{1}, {7, 8, 9}), "unselected nodes still evaluated");
    requireMatrix(pose.joints[1][0],
                  glm::translate(glm::mat4{1}, {1, 1.5F, 1}) * glm::scale(glm::mat4{1}, {-1, 0.5F, 1}),
                  "mesh-local joint matrix / inverse bind / later descendant");
    requireMatrix(pose.joints[1][1],
                  glm::translate(glm::mat4{1}, {1, 2, 0}) * glm::scale(glm::mat4{1}, {-1, 1, 1}),
                  "joint order / nonuniform mirrored mesh transform");
    const auto preview = glm::translate(glm::mat4{1}, {0, 4, -2});
    requireMatrix(pose.joints[3][0], preview, "bind-pose preview first joint");
    requireMatrix(pose.joints[3][1], preview, "bind-pose preview repeats first joint without inverse binds");

    const auto* localStorage = pose.local.data();
    const auto* worldStorage = pose.world.data();
    const auto* jointStorage = pose.joints[1].data();
    const auto* weightStorage = pose.weights[1].data();

    for (int sample = 0; sample < 100; ++sample) {
        instance.seek(static_cast<float>(sample) / 100);
    }

    require(pose.local.data() == localStorage && pose.world.data() == worldStorage &&
                pose.joints[1].data() == jointStorage && pose.weights[1].data() == weightStorage,
            "steady evaluation replaced pose storage");
    instance.stop();
    require(near(pose.world[4][3].y, 6) && near(pose.weights[1][0], 0.5F), "restore hierarchy and morph rest pose");
}

template <class Function> void requireError(Function&& function, std::string_view expected) {
    try {
        function();
    } catch (const std::runtime_error& error) {
        require(error.what() == expected, "animation validation diagnostic changed");
        return;
    }
    throw std::runtime_error("invalid animation evaluation was accepted");
}

void invalidEvaluation() {
    auto sampleModel = makeModel();
    addChannel(sampleModel->animations[0],
               0,
               AnimationPath::translation,
               {{0}, {std::numeric_limits<float>::infinity(), 0, 0}, 3});
    ModelInstance sample{sampleModel};
    requireError(
        [&] {
            sample.play();
        },
        "Non-finite animation sample.");

    auto rotationModel = makeModel();
    addChannel(rotationModel->animations[0], 0, AnimationPath::rotation, {{0}, {0, 0, 0, 0}, 4, Interpolation::step});
    ModelInstance rotation{rotationModel};
    requireError(
        [&] {
            rotation.play();
        },
        "Animation produced a zero quaternion.");

    auto worldModel = makeModel();
    worldModel->nodes[0].scale = {2, 1, 1};
    worldModel->nodes[0].children = {1};
    worldModel->nodes.emplace_back();
    worldModel->nodes[1].parent = 0;
    worldModel->nodes[1].matrixTransform = false;
    worldModel->nodes[1].name = "overflow";
    addChannel(worldModel->animations[0],
               1,
               AnimationPath::translation,
               {{0}, {std::numeric_limits<float>::max(), 0, 0}, 3, Interpolation::step});
    ModelInstance world{worldModel};
    requireError(
        [&] {
            world.play();
        },
        "Non-finite animated world transform: overflow");

    auto skinModel = makeModel();
    skinModel->nodes[0].skin = 0;
    skinModel->skins.push_back({"invalid", {0}, {glm::mat4{1}}});
    addChannel(skinModel->animations[0], 0, AnimationPath::scale, {{0}, {0, 1, 1}, 3});
    ModelInstance singular{skinModel};
    requireError(
        [&] {
            singular.play();
        },
        "Singular animated skinned mesh node.");

    auto invalidSkinModel = std::make_shared<ModelData>(*skinModel);
    invalidSkinModel->skins[0].inverseBindMatrices[0][0][0] = std::numeric_limits<float>::infinity();
    requireError(
        [&] {
            ModelInstance invalidJoint{invalidSkinModel};
        },
        "Non-finite joint matrix in skin: invalid");
}

} // namespace

int main() {
    try {
        samplingPaths();
        quaternionModesAndSingleKeys();
        hierarchyAndSkinning();
        invalidEvaluation();
        std::cout << "Animation evaluation checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
