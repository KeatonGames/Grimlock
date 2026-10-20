#include "assets/gltf_animation.hpp"

#include "core/startup_profile.hpp"
#include "grimlock/assets/model_data.hpp"

#include <algorithm>
#include <cmath>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace grimlock::assets {

namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

SkinData readSkin(const fastgltf::Asset& asset, const fastgltf::Skin& source, std::size_t nodeCount) {
    SkinData skin;
    skin.name = source.name;
    skin.joints.assign(source.joints.begin(), source.joints.end());

    if (skin.joints.empty()) {
        fail("Skin '" + skin.name + "' has no joints");
    }

    std::set<std::size_t> uniqueJoints;

    for (const auto joint : skin.joints) {
        if (joint >= nodeCount || !uniqueJoints.insert(joint).second) {
            fail("Skin '" + skin.name + "': invalid/repeated joint");
        }
    }

    if (source.skeleton) {
        if (*source.skeleton >= nodeCount) {
            fail("Invalid skin skeleton");
        }

        skin.skeleton = *source.skeleton;
    }

    skin.inverseBindMatrices.resize(skin.joints.size(), glm::mat4{1});

    if (!source.inverseBindMatrices) {
        return skin;
    }

    const auto& accessor = asset.accessors.at(*source.inverseBindMatrices);

    if (accessor.type != fastgltf::AccessorType::Mat4 || accessor.componentType != fastgltf::ComponentType::Float ||
        accessor.count < skin.joints.size()) {
        fail("Skin '" + skin.name + "': invalid inverse bind accessor");
    }

    fastgltf::iterateAccessorWithIndex<glm::mat4>(asset, accessor, [&](auto matrix, std::size_t index) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!std::isfinite(matrix[column][row])) {
                    fail("Non-finite inverse bind matrix");
                }
            }
        }

        if (index < skin.joints.size()) {
            skin.inverseBindMatrices[index] = matrix;
        }
    });

    return skin;
}

LightData readLight(const fastgltf::Light& source) {
    LightData light;
    light.name = source.name;

    switch (source.type) {
    case fastgltf::LightType::Directional:
        light.type = LightType::directional;
        break;
    case fastgltf::LightType::Point:
        light.type = LightType::point;
        break;
    case fastgltf::LightType::Spot:
        light.type = LightType::spot;
        break;
    default:
        fail("Invalid punctual light type");
    }

    light.color = {source.color[0], source.color[1], source.color[2]};
    light.intensity = source.intensity;
    light.range = source.range.value_or(0);
    light.innerCone = source.innerConeAngle.value_or(0);
    light.outerCone = source.outerConeAngle.value_or(0.785398163F);

    if (!std::isfinite(light.intensity) || light.intensity < 0 || !std::isfinite(light.range) || light.range < 0 ||
        (source.range && light.range == 0) || !std::isfinite(light.innerCone) || !std::isfinite(light.outerCone) ||
        light.innerCone < 0 || light.innerCone >= light.outerCone || light.outerCone > 1.570796327F) {
        fail("Invalid punctual light '" + light.name + "'");
    }

    for (int component = 0; component < 3; ++component) {
        if (!std::isfinite(light.color[component]) || light.color[component] < 0) {
            fail("Invalid light color");
        }
    }

    return light;
}

AnimationPath readAnimationPath(fastgltf::AnimationPath path, const std::string& clipName) {
    switch (path) {
    case fastgltf::AnimationPath::Translation:
        return AnimationPath::translation;
    case fastgltf::AnimationPath::Rotation:
        return AnimationPath::rotation;
    case fastgltf::AnimationPath::Scale:
        return AnimationPath::scale;
    case fastgltf::AnimationPath::Weights:
        return AnimationPath::weights;
    default:
        fail("Animation '" + clipName + "': unsupported target path");
    }
}

Interpolation readInterpolation(fastgltf::AnimationInterpolation interpolation) {
    switch (interpolation) {
    case fastgltf::AnimationInterpolation::Step:
        return Interpolation::step;
    case fastgltf::AnimationInterpolation::Linear:
        return Interpolation::linear;
    case fastgltf::AnimationInterpolation::CubicSpline:
        return Interpolation::cubicSpline;
    default:
        fail("Unknown animation interpolation");
    }
}

void readAnimationValues(const fastgltf::Asset& asset,
                         const fastgltf::Accessor& output,
                         AnimationPath path,
                         std::size_t valuesPerKey,
                         AnimationSampler& sampler) {
    const auto add = [&](float value) {
        if (!std::isfinite(value)) {
            fail("Non-finite animation output");
        }

        sampler.values.push_back(value);
    };

    if (path == AnimationPath::weights) {
        fastgltf::iterateAccessor<float>(asset, output, [&](float value) {
            add(value);
        });
        return;
    }

    if (path == AnimationPath::rotation) {
        fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, output, [&](auto value, std::size_t index) {
            if (valuesPerKey == 1 || index % 3 == 1) {
                const float length = glm::length(value);

                if (std::abs(length - 1) > 1e-3F) {
                    fail("Non-unit animation quaternion");
                }
            }

            for (int component = 0; component < 4; ++component) {
                add(value[component]);
            }
        });
        return;
    }

    fastgltf::iterateAccessor<glm::vec3>(asset, output, [&](auto value) {
        for (int component = 0; component < 3; ++component) {
            add(value[component]);
        }
    });
}

void readAnimationSampler(const fastgltf::Asset& asset,
                          const fastgltf::AnimationSampler& source,
                          AnimationPath path,
                          const std::string& clipName,
                          AnimationSampler& sampler) {
    const auto& input = asset.accessors.at(source.inputAccessor);
    const auto& output = asset.accessors.at(source.outputAccessor);

    if (input.type != fastgltf::AccessorType::Scalar || input.componentType != fastgltf::ComponentType::Float ||
        output.componentType != fastgltf::ComponentType::Float || input.normalized || output.normalized) {
        fail("Animation '" + clipName + "': invalid accessor types");
    }

    sampler.interpolation = readInterpolation(source.interpolation);
    float previousTime = -1;

    fastgltf::iterateAccessor<float>(asset, input, [&](float time) {
        if (!std::isfinite(time) || time < 0 || time <= previousTime) {
            fail("Animation '" + clipName + "': key times must increase and be nonnegative");
        }

        previousTime = time;
        sampler.times.push_back(time);
    });

    const std::size_t valuesPerKey = sampler.interpolation == Interpolation::cubicSpline ? 3 : 1;
    const auto expected = sampler.times.size() * valuesPerKey;

    if (output.count != expected * (path == AnimationPath::weights ? sampler.components : 1) ||
        (path == AnimationPath::weights    ? output.type != fastgltf::AccessorType::Scalar
         : path == AnimationPath::rotation ? output.type != fastgltf::AccessorType::Vec4
                                           : output.type != fastgltf::AccessorType::Vec3)) {
        fail("Animation '" + clipName + "': output accessor count/type mismatch");
    }

    readAnimationValues(asset, output, path, valuesPerKey, sampler);
}

AnimationData readAnimationClip(const fastgltf::Asset& asset,
                                const fastgltf::Animation& source,
                                const ModelData& model) {
    AnimationData clip;
    clip.name = source.name;
    clip.start = std::numeric_limits<float>::max();
    clip.end = 0;
    clip.samplers.resize(source.samplers.size());
    std::set<std::pair<std::size_t, AnimationPath>> targets;

    for (const auto& channel : source.channels) {
        // glTF permits channels without a target node; they do not animate anything.
        if (!channel.nodeIndex) {
            continue;
        }

        if (*channel.nodeIndex >= model.nodes.size() || channel.samplerIndex >= source.samplers.size()) {
            fail("Animation '" + clip.name + "': invalid channel reference");
        }

        const auto path = readAnimationPath(channel.path, clip.name);
        const auto nodeIndex = *channel.nodeIndex;

        if (path != AnimationPath::weights && model.nodes[nodeIndex].matrixTransform) {
            fail("Animation targets a matrix node: " + model.nodes[nodeIndex].name);
        }

        if (!targets.insert({nodeIndex, path}).second) {
            fail("Animation has duplicate node/property channels");
        }

        const auto components = path == AnimationPath::rotation ? 4U
                                : path == AnimationPath::weights
                                    ? static_cast<std::uint32_t>(model.nodes[nodeIndex].weights.size())
                                    : 3U;
        if (components == 0) {
            fail("Weight animation targets a mesh without morph targets");
        }

        auto& sampler = clip.samplers[channel.samplerIndex];

        if (sampler.components && sampler.components != components) {
            fail("Animation sampler reused with incompatible targets");
        }

        if (!sampler.components) {
            sampler.components = components;
            readAnimationSampler(asset, source.samplers[channel.samplerIndex], path, clip.name, sampler);
            clip.start = std::min(clip.start, sampler.times.front());
            clip.end = std::max(clip.end, sampler.times.back());
        }

        clip.channels.push_back({channel.samplerIndex, nodeIndex, path});
    }

    if (clip.channels.empty()) {
        clip.start = clip.end = 0;
    }

    return clip;
}

} // namespace

void loadGltfAnimation(const fastgltf::Asset& asset, ModelData& model) {
    diagnostics::StartupSpan profile{"asset.animation_skin_light_conversion"};

    for (const auto& source : asset.skins) {
        model.skins.push_back(readSkin(asset, source, model.nodes.size()));
    }

    for (const auto& source : asset.lights) {
        model.lights.push_back(readLight(source));
    }

    for (const auto& source : asset.animations) {
        model.animations.push_back(readAnimationClip(asset, source, model));
    }
}

} // namespace grimlock::assets
