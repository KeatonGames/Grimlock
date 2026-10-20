#include "assets/gltf_animation.hpp"

#include "core/startup_profile.hpp"
#include "grimlock/assets/model_data.hpp"

#include <algorithm>
#include <cmath>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <set>
#include <stdexcept>
#include <utility>

namespace grimlock::assets {
void loadGltfAnimation(const fastgltf::Asset& asset, ModelData& model) {
    diagnostics::StartupSpan profile{"asset.animation_skin_light_conversion"};
    const auto fail = [](const std::string& message) { throw std::runtime_error(message); };
    for (const auto& source : asset.skins) {
        SkinData skin;
        skin.name = source.name;
        skin.joints.assign(source.joints.begin(), source.joints.end());
        if (skin.joints.empty()) {
            fail("Skin '" + skin.name + "' has no joints");
        }
        std::set<std::size_t> unique;
        for (const auto joint : skin.joints) {
            if (joint >= model.nodes.size() || !unique.insert(joint).second) {
                fail("Skin '" + skin.name + "': invalid/repeated joint");
            }
        }
        if (source.skeleton) {
            if (*source.skeleton >= model.nodes.size()) {
                fail("Invalid skin skeleton");
            }
            skin.skeleton = *source.skeleton;
        }
        skin.inverseBindMatrices.resize(skin.joints.size(), glm::mat4{1});
        if (source.inverseBindMatrices) {
            const auto& accessor = asset.accessors.at(*source.inverseBindMatrices);
            if (accessor.type != fastgltf::AccessorType::Mat4 ||
                accessor.componentType != fastgltf::ComponentType::Float ||
                accessor.count < skin.joints.size()) {
                fail("Skin '" + skin.name + "': invalid inverse bind accessor");
            }
            fastgltf::iterateAccessorWithIndex<glm::mat4>(
                asset, accessor, [&](auto matrix, std::size_t index) {
                    for (int c = 0; c < 4; ++c) {
                        for (int r = 0; r < 4; ++r) {
                            if (!std::isfinite(matrix[c][r])) {
                                fail("Non-finite inverse bind matrix");
                            }
                        }
                    }
                    if (index < skin.joints.size()) {
                        skin.inverseBindMatrices[index] = matrix;
                    }
                });
        }
        model.skins.push_back(std::move(skin));
    }
    for (const auto& source : asset.lights) {
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
        if (!std::isfinite(light.intensity) || light.intensity < 0 || !std::isfinite(light.range) ||
            light.range < 0 || (source.range && light.range == 0) ||
            !std::isfinite(light.innerCone) || !std::isfinite(light.outerCone) ||
            light.innerCone < 0 || light.innerCone >= light.outerCone ||
            light.outerCone > 1.570796327F) {
            fail("Invalid punctual light '" + light.name + "'");
        }
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(light.color[c]) || light.color[c] < 0) {
                fail("Invalid light color");
            }
        }
        model.lights.push_back(std::move(light));
    }
    for (const auto& source : asset.animations) {
        AnimationData clip;
        clip.name = source.name;
        clip.start = std::numeric_limits<float>::max();
        clip.end = 0;
        clip.samplers.resize(source.samplers.size());
        std::set<std::pair<std::size_t, AnimationPath>> targets;
        for (const auto& channel : source.channels) {
            if (!channel.nodeIndex) {
                continue;
            } // glTF permits channels without a target node; they do not animate anything.
            if (*channel.nodeIndex >= model.nodes.size() ||
                channel.samplerIndex >= source.samplers.size()) {
                fail("Animation '" + clip.name + "': invalid channel reference");
            }
            AnimationPath path = AnimationPath::translation;
            switch (channel.path) {
            case fastgltf::AnimationPath::Translation:
                path = AnimationPath::translation;
                break;
            case fastgltf::AnimationPath::Rotation:
                path = AnimationPath::rotation;
                break;
            case fastgltf::AnimationPath::Scale:
                path = AnimationPath::scale;
                break;
            case fastgltf::AnimationPath::Weights:
                path = AnimationPath::weights;
                break;
            default:
                fail("Animation '" + clip.name + "': unsupported target path");
            }
            const auto node = *channel.nodeIndex;
            if (path != AnimationPath::weights && model.nodes[node].matrixTransform) {
                fail("Animation targets a matrix node: " + model.nodes[node].name);
            }
            if (!targets.insert({node, path}).second) {
                fail("Animation has duplicate node/property channels");
            }
            const auto components =
                path == AnimationPath::rotation ? 4U
                : path == AnimationPath::weights
                    ? static_cast<std::uint32_t>(model.nodes[node].weights.size())
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
                const auto& original = source.samplers[channel.samplerIndex];
                const auto &input = asset.accessors.at(original.inputAccessor),
                           &output = asset.accessors.at(original.outputAccessor);
                if (input.type != fastgltf::AccessorType::Scalar ||
                    input.componentType != fastgltf::ComponentType::Float ||
                    output.componentType != fastgltf::ComponentType::Float || input.normalized ||
                    output.normalized) {
                    fail("Animation '" + clip.name + "': invalid accessor types");
                }
                switch (original.interpolation) {
                case fastgltf::AnimationInterpolation::Step:
                    sampler.interpolation = Interpolation::step;
                    break;
                case fastgltf::AnimationInterpolation::Linear:
                    sampler.interpolation = Interpolation::linear;
                    break;
                case fastgltf::AnimationInterpolation::CubicSpline:
                    sampler.interpolation = Interpolation::cubicSpline;
                    break;
                default:
                    fail("Unknown animation interpolation");
                }
                float previous = -1;
                fastgltf::iterateAccessor<float>(asset, input, [&](float time) {
                    if (!std::isfinite(time) || time < 0 || time <= previous) {
                        fail("Animation '" + clip.name +
                             "': key times must increase and be nonnegative");
                    }
                    previous = time;
                    sampler.times.push_back(time);
                });
                const std::size_t factor =
                    sampler.interpolation == Interpolation::cubicSpline ? 3 : 1;
                const auto expected = sampler.times.size() * factor;
                if (output.count != expected * (path == AnimationPath::weights ? components : 1) ||
                    (path == AnimationPath::weights ? output.type != fastgltf::AccessorType::Scalar
                     : path == AnimationPath::rotation
                         ? output.type != fastgltf::AccessorType::Vec4
                         : output.type != fastgltf::AccessorType::Vec3)) {
                    fail("Animation '" + clip.name + "': output accessor count/type mismatch");
                }
                const auto add = [&](float value) {
                    if (!std::isfinite(value)) {
                        fail("Non-finite animation output");
                    }
                    sampler.values.push_back(value);
                };
                if (path == AnimationPath::weights) {
                    fastgltf::iterateAccessor<float>(asset, output,
                                                     [&](float value) { add(value); });
                } else if (path == AnimationPath::rotation) {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(
                        asset, output, [&](auto value, std::size_t index) {
                            if (factor == 1 || index % 3 == 1) {
                                const float length = glm::length(value);
                                if (std::abs(length - 1) > 1e-3F) {
                                    fail("Non-unit animation quaternion");
                                }
                            }
                            for (int c = 0; c < 4; ++c) {
                                add(value[c]);
                            }
                        });
                } else {
                    fastgltf::iterateAccessor<glm::vec3>(asset, output, [&](auto value) {
                        for (int c = 0; c < 3; ++c) {
                            add(value[c]);
                        }
                    });
                }
                clip.start = std::min(clip.start, sampler.times.front());
                clip.end = std::max(clip.end, sampler.times.back());
            }
            clip.channels.push_back({channel.samplerIndex, node, path});
        }
        if (clip.channels.empty()) {
            clip.start = clip.end = 0;
        }
        model.animations.push_back(std::move(clip));
    }
}
} // namespace grimlock::assets
