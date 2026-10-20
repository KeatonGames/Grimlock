#include "assets/gltf_animation.hpp"

#include "grimlock/assets/model_data.hpp"

#include <cstring>
#include <fastgltf/types.hpp>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace grimlock::assets;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t addAccessor(fastgltf::Asset& asset, fastgltf::AccessorType type, std::initializer_list<float> values) {
    const auto byteLength = values.size() * sizeof(float);
    fastgltf::sources::Array data{fastgltf::StaticVector<std::byte>(byteLength)};
    std::memcpy(data.bytes.data(), values.begin(), byteLength);

    fastgltf::Buffer buffer;
    buffer.byteLength = byteLength;
    buffer.data = std::move(data);
    asset.buffers.push_back(std::move(buffer));

    fastgltf::BufferView view;
    view.bufferIndex = asset.buffers.size() - 1;
    view.byteLength = byteLength;
    asset.bufferViews.push_back(std::move(view));

    fastgltf::Accessor accessor;
    accessor.count = values.size() / fastgltf::getNumComponents(type);
    accessor.type = type;
    accessor.componentType = fastgltf::ComponentType::Float;
    accessor.bufferViewIndex = asset.bufferViews.size() - 1;
    asset.accessors.push_back(std::move(accessor));
    return asset.accessors.size() - 1;
}

ModelData makeModel() {
    ModelData model;
    model.nodes.resize(3);
    model.nodes[0].matrixTransform = false;
    model.nodes[0].name = "moving";
    model.nodes[1].matrixTransform = false;
    model.nodes[2].weights = {0, 0};
    return model;
}

fastgltf::Asset makeAnimationAsset() {
    fastgltf::Asset asset;
    fastgltf::Animation clip;
    clip.name = "movement";

    const auto vectors = addAccessor(asset, fastgltf::AccessorType::Vec3, {1, 2, 3, 4, 5, 6});
    const auto vectorTimes = addAccessor(asset, fastgltf::AccessorType::Scalar, {0.5F, 2});
    clip.samplers.push_back({vectorTimes, vectors, fastgltf::AnimationInterpolation::Linear});
    clip.channels.push_back({0, std::size_t{0}, fastgltf::AnimationPath::Translation});
    clip.channels.push_back({0, std::size_t{1}, fastgltf::AnimationPath::Scale});

    const auto rotations = addAccessor(asset, fastgltf::AccessorType::Vec4, {0, 0, 0, 1, 0, 0, 1, 0});
    const auto rotationTimes = addAccessor(asset, fastgltf::AccessorType::Scalar, {1, 3});
    clip.samplers.push_back({rotationTimes, rotations, fastgltf::AnimationInterpolation::Step});
    clip.channels.push_back({1, std::size_t{0}, fastgltf::AnimationPath::Rotation});

    const auto weights =
        addAccessor(asset, fastgltf::AccessorType::Scalar, {1, 2, 0.1F, 0.2F, 3, 4, 5, 6, 0.3F, 0.4F, 7, 8});
    const auto weightTimes = addAccessor(asset, fastgltf::AccessorType::Scalar, {2, 4});
    clip.samplers.push_back({weightTimes, weights, fastgltf::AnimationInterpolation::CubicSpline});
    clip.channels.push_back({2, std::size_t{2}, fastgltf::AnimationPath::Weights});

    // Cubic tangents are not unit quaternions; only each key's value must be unit length.
    const auto cubicRotations = addAccessor(asset, fastgltf::AccessorType::Vec4, {2, 0, 0, 0, 0, 0, 0, 1, 3, 0, 0, 0,
                                                                                  4, 0, 0, 0, 0, 0, 1, 0, 5, 0, 0, 0});
    clip.samplers.push_back({rotationTimes, cubicRotations, fastgltf::AnimationInterpolation::CubicSpline});
    clip.channels.push_back({3, std::size_t{1}, fastgltf::AnimationPath::Rotation});

    // Unreferenced samplers and channels without target nodes are not decoded.
    clip.samplers.push_back({999, 999});
    clip.channels.push_back({999, {}, fastgltf::AnimationPath::Translation});
    asset.animations.push_back(std::move(clip));

    fastgltf::Animation empty;
    empty.name = "empty";
    empty.samplers.push_back({999, 999});
    empty.channels.push_back({999, {}, fastgltf::AnimationPath::Translation});
    asset.animations.push_back(std::move(empty));
    return asset;
}

void checkClips() {
    const auto asset = makeAnimationAsset();
    auto model = makeModel();
    loadGltfAnimation(asset, model);

    require(model.animations.size() == 2, "clip count changed");
    const auto& clip = model.animations[0];
    require(clip.name == "movement" && clip.start == 0.5F && clip.end == 4, "clip name or time range changed");
    require(clip.channels.size() == 5 && clip.samplers.size() == 5, "channel/sampler count changed");
    require(clip.channels[0].node == 0 && clip.channels[0].path == AnimationPath::translation &&
                clip.channels[1].node == 1 && clip.channels[1].path == AnimationPath::scale &&
                clip.channels[0].sampler == clip.channels[1].sampler &&
                clip.channels[2].path == AnimationPath::rotation && clip.channels[3].path == AnimationPath::weights,
            "channel ordering, paths, or shared sampler mapping changed");

    const auto& linear = clip.samplers[0];
    require(linear.components == 3 && linear.interpolation == Interpolation::linear &&
                linear.times == std::vector<float>{0.5F, 2} && linear.values == std::vector<float>{1, 2, 3, 4, 5, 6},
            "linear vector decoding changed");
    require(clip.samplers[1].components == 4 && clip.samplers[1].interpolation == Interpolation::step &&
                clip.samplers[1].values == std::vector<float>{0, 0, 0, 1, 0, 0, 1, 0},
            "step quaternion order changed");
    require(clip.samplers[2].components == 2 && clip.samplers[2].interpolation == Interpolation::cubicSpline &&
                clip.samplers[2].values == std::vector<float>{1, 2, 0.1F, 0.2F, 3, 4, 5, 6, 0.3F, 0.4F, 7, 8},
            "cubic morph in/value/out ordering changed");
    require(clip.samplers[3].values ==
                std::vector<float>{2, 0, 0, 0, 0, 0, 0, 1, 3, 0, 0, 0, 4, 0, 0, 0, 0, 0, 1, 0, 5, 0, 0, 0},
            "cubic quaternion values or tangents changed");
    require(clip.samplers[4].components == 0 && clip.samplers[4].times.empty(), "unused sampler was decoded");
    const auto& empty = model.animations[1];
    require(empty.name == "empty" && empty.start == 0 && empty.end == 0 && empty.channels.empty(),
            "empty animation range changed");
}

void checkResources() {
    fastgltf::Asset asset;
    fastgltf::Skin skin;
    skin.name = "identity";
    skin.joints = {0, 1};
    skin.skeleton = std::size_t{0};
    asset.skins.push_back(skin);
    skin.name = "explicit";
    // The third matrix is validated but only the two joints' matrices are stored.
    skin.inverseBindMatrices = addAccessor(asset,
                                           fastgltf::AccessorType::Mat4,
                                           {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0,
                                            0, 0, 1, 0, 2, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3, 0, 0, 1});
    asset.skins.push_back(std::move(skin));

    for (const auto type : {fastgltf::LightType::Directional, fastgltf::LightType::Point, fastgltf::LightType::Spot}) {
        fastgltf::Light light{};
        light.name = "light";
        light.type = type;
        light.color = {0.25F, 0.5F, 1};
        light.intensity = 2;
        asset.lights.push_back(light);
    }
    asset.lights.back().range = 10;
    asset.lights.back().innerConeAngle = 0.1F;
    asset.lights.back().outerConeAngle = 0.5F;

    auto model = makeModel();
    loadGltfAnimation(asset, model);
    require(model.skins.size() == 2 && model.skins[0].name == "identity" && model.skins[0].skeleton == 0 &&
                model.skins[0].joints == std::vector<std::size_t>{0, 1} &&
                model.skins[0].inverseBindMatrices == std::vector<glm::mat4>(2, glm::mat4{1}),
            "skin joint order, skeleton, or default inverse binds changed");
    require(model.skins[1].inverseBindMatrices.size() == 2 &&
                model.skins[1].inverseBindMatrices[0][3] == glm::vec4{1, 0, 0, 1} &&
                model.skins[1].inverseBindMatrices[1][3] == glm::vec4{2, 0, 0, 1},
            "explicit inverse bind matrix order changed");
    require(model.lights.size() == 3 && model.lights[0].type == LightType::directional &&
                model.lights[1].type == LightType::point && model.lights[2].type == LightType::spot &&
                model.lights[0].color == glm::vec3{0.25F, 0.5F, 1} && model.lights[0].intensity == 2 &&
                model.lights[0].range == 0 && model.lights[0].innerCone == 0 &&
                model.lights[0].outerCone == 0.785398163F && model.lights[2].range == 10 &&
                model.lights[2].innerCone == 0.1F && model.lights[2].outerCone == 0.5F,
            "light type conversion or optional defaults changed");
}

template <typename Edit> void rejects(Edit edit, std::string_view expected) {
    auto asset = makeAnimationAsset();
    auto model = makeModel();
    edit(asset, model);
    try {
        loadGltfAnimation(asset, model);
    } catch (const std::runtime_error& error) {
        if (error.what() != expected) {
            throw std::runtime_error("Expected '" + std::string{expected} + "'; got: " + error.what());
        }
        return;
    }
    throw std::runtime_error("invalid animation import was accepted");
}

void checkValidation() {
    rejects(
        [](auto& asset, auto&) {
            asset.animations[0].channels[0].nodeIndex = std::size_t{9};
        },
        "Animation 'movement': invalid channel reference");
    rejects(
        [](auto&, auto& model) {
            model.nodes[0].matrixTransform = true;
        },
        "Animation targets a matrix node: moving");
    rejects(
        [](auto& asset, auto&) {
            asset.animations[0].channels.push_back(asset.animations[0].channels[0]);
        },
        "Animation has duplicate node/property channels");
    rejects(
        [](auto& asset, auto&) {
            asset.animations[0].channels[2].samplerIndex = 0;
        },
        "Animation sampler reused with incompatible targets");
    rejects(
        [](auto&, auto& model) {
            model.nodes[2].weights.clear();
        },
        "Weight animation targets a mesh without morph targets");
    rejects(
        [](auto& asset, auto&) {
            asset.accessors[1].normalized = true;
        },
        "Animation 'movement': invalid accessor types");
    rejects(
        [](auto& asset, auto&) {
            asset.animations[0].samplers[0].inputAccessor = addAccessor(asset, fastgltf::AccessorType::Scalar, {1, 1});
        },
        "Animation 'movement': key times must increase and be nonnegative");
    rejects(
        [](auto& asset, auto&) {
            asset.animations[0].samplers[0].inputAccessor = addAccessor(asset, fastgltf::AccessorType::Scalar, {-1, 1});
            asset.accessors[0].count = 1;
        },
        "Animation 'movement': key times must increase and be nonnegative");
    rejects(
        [](auto& asset, auto&) {
            asset.accessors[0].count = 1;
        },
        "Animation 'movement': output accessor count/type mismatch");
    rejects(
        [](auto& asset, auto&) {
            asset.animations[0].samplers[0].outputAccessor =
                addAccessor(asset,
                            fastgltf::AccessorType::Vec3,
                            {1, 2, 3, 4, 5, std::numeric_limits<float>::infinity()});
        },
        "Non-finite animation output");
    rejects(
        [](auto& asset, auto&) {
            asset.animations[0].samplers[1].outputAccessor =
                addAccessor(asset, fastgltf::AccessorType::Vec4, {0, 0, 0, 2, 0, 0, 1, 0});
        },
        "Non-unit animation quaternion");
    rejects(
        [](auto& asset, auto&) {
            fastgltf::Skin skin;
            skin.name = "bad";
            skin.joints = {0, 0};
            asset.skins.push_back(std::move(skin));
            asset.animations[0].channels[0].nodeIndex = std::size_t{9};
        },
        "Skin 'bad': invalid/repeated joint");
    rejects(
        [](auto& asset, auto&) {
            fastgltf::Light light{};
            light.type = fastgltf::LightType::Point;
            light.intensity = 1;
            light.range = 0;
            asset.lights.push_back(light);
        },
        "Invalid punctual light ''");
}

} // namespace

int main() {
    try {
        checkClips();
        checkResources();
        checkValidation();
        std::cout << "PASS glTF animation import: channels, samplers, cubic data, skins, lights and validation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL glTF animation import: " << error.what() << '\n';
        return 1;
    }
}
