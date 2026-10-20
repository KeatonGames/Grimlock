#include "grimlock/assets/animation.hpp"

#include "core/startup_profile.hpp"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <stdexcept>
#include <utility>

namespace grimlock::assets {

namespace {

bool isFinite(const glm::mat4& matrix) noexcept {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }

    return true;
}

} // namespace

ModelInstance::ModelInstance(std::shared_ptr<const ModelData> model) : model_(std::move(model)) {
    diagnostics::StartupSpan profile{"animation.instance_prepare"};

    if (!model_) {
        throw std::invalid_argument("ModelInstance requires an asset.");
    }

    for (const auto& node : modelNodeTransforms(*model_)) {
        pose_.activeNodes.push_back(node.node);
    }

    const auto count = model_->nodes.size();

    for (std::size_t n = 0; n < count; ++n) {
        if (!model_->nodes[n].parent) {
            order_.push_back(n);
        }
    }

    for (std::size_t cursor = 0; cursor < order_.size(); ++cursor) {
        const auto& children = model_->nodes[order_[cursor]].children;
        order_.insert(order_.end(), children.begin(), children.end());
    }

    pose_.local.resize(count);
    pose_.world.resize(count);
    pose_.weights.resize(count);
    pose_.joints.resize(count);
    translations_.resize(count);
    scales_.resize(count);
    rotations_.resize(count);

    for (std::size_t n = 0; n < count; ++n) {
        pose_.weights[n] = model_->nodes[n].weights;

        if (model_->nodes[n].skin) {
            pose_.joints[n].resize(model_->skins.at(*model_->nodes[n].skin).joints.size());
        }
    }

    evaluate(false);
}

void ModelInstance::play(std::size_t clip, bool loop) {
    if (clip >= model_->animations.size()) {
        throw std::out_of_range("Animation clip is out of range.");
    }

    clip_ = clip;
    loop_ = loop;
    time_ = model_->animations[clip].start;
    playing_ = true;
    paused_ = false;
    evaluate(true);
}

void ModelInstance::stop(bool restoreRestPose) {
    playing_ = false;
    paused_ = false;

    if (restoreRestPose) {
        evaluate(false);
    }
}

void ModelInstance::setSpeed(float speed) {
    if (!std::isfinite(speed)) {
        throw std::invalid_argument("Animation speed must be finite.");
    }

    speed_ = speed;
}

void ModelInstance::seek(float seconds) {
    if (!std::isfinite(seconds) || clip_ >= model_->animations.size()) {
        throw std::invalid_argument("Invalid animation seek.");
    }

    const auto& clip = model_->animations[clip_];
    time_ = std::clamp(seconds, clip.start, clip.end);
    evaluate(true);
}

void ModelInstance::update(double deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0) {
        throw std::invalid_argument("Animation delta must be finite and nonnegative.");
    }

    if (!playing_ || paused_) {
        return;
    }

    const auto& clip = model_->animations[clip_];
    const double duration = clip.end - clip.start, next = time_ + deltaSeconds * speed_;

    if (loop_ && duration > 0) {
        time_ = clip.start + static_cast<float>(std::fmod(std::fmod(next - clip.start, duration) + duration, duration));
    } else {
        time_ = static_cast<float>(std::clamp(next, static_cast<double>(clip.start), static_cast<double>(clip.end)));

        if ((speed_ >= 0 && next >= clip.end) || (speed_ < 0 && next <= clip.start)) {
            playing_ = false;
        }
    }

    evaluate(true);
}

void ModelInstance::evaluate(bool animated) {
    animatedPose_ = animated;
    resetPoseChannels();

    if (animated) {
        const auto& clip = model_->animations.at(clip_);

        for (const auto& channel : clip.channels) {
            applyAnimationChannel(channel, clip.samplers[channel.sampler]);
        }
    }

    updateNodeTransforms();
    updateSkinMatrices();
}

void ModelInstance::resetPoseChannels() {
    for (std::size_t nodeIndex = 0; nodeIndex < model_->nodes.size(); ++nodeIndex) {
        const auto& node = model_->nodes[nodeIndex];
        translations_[nodeIndex] = node.translation;
        rotations_[nodeIndex] = node.rotation;
        scales_[nodeIndex] = node.scale;
        pose_.weights[nodeIndex] = node.weights;
    }
}

void ModelInstance::applyAnimationChannel(const AnimationChannel& channel, const AnimationSampler& sampler) {
    const auto components = sampler.components;
    const auto upperKey = std::upper_bound(sampler.times.begin(), sampler.times.end(), time_);
    const std::size_t leftKey =
        upperKey == sampler.times.begin() ? 0 : static_cast<std::size_t>(upperKey - sampler.times.begin() - 1);
    const std::size_t rightKey = std::min(leftKey + 1, sampler.times.size() - 1);
    const float interval = sampler.times[rightKey] - sampler.times[leftKey];
    const float fraction = interval > 0 ? std::clamp((time_ - sampler.times[leftKey]) / interval, 0.F, 1.F) : 0;
    const bool cubic = sampler.interpolation == Interpolation::cubicSpline;

    const auto sampleValue = [&](std::size_t key, std::size_t component, std::size_t part = 1) {
        return sampler.values[(key * (cubic ? 3 : 1) + (cubic ? part : 0)) * components + component];
    };

    sampled_.resize(components);

    if (channel.path == AnimationPath::rotation && sampler.interpolation == Interpolation::linear) {
        const glm::quat leftRotation{sampleValue(leftKey, 3),
                                     sampleValue(leftKey, 0),
                                     sampleValue(leftKey, 1),
                                     sampleValue(leftKey, 2)};
        const glm::quat rightRotation{sampleValue(rightKey, 3),
                                      sampleValue(rightKey, 0),
                                      sampleValue(rightKey, 1),
                                      sampleValue(rightKey, 2)};

        rotations_[channel.node] = glm::normalize(glm::slerp(leftRotation, rightRotation, fraction));
        return;
    }

    for (std::size_t component = 0; component < components; ++component) {
        float result = sampleValue(leftKey, component);

        if (cubic && leftKey != rightKey) {
            const float fractionSquared = fraction * fraction;
            const float fractionCubed = fractionSquared * fraction;

            result = (2 * fractionCubed - 3 * fractionSquared + 1) * sampleValue(leftKey, component) +
                     (fractionCubed - 2 * fractionSquared + fraction) * interval * sampleValue(leftKey, component, 2) +
                     (-2 * fractionCubed + 3 * fractionSquared) * sampleValue(rightKey, component) +
                     (fractionCubed - fractionSquared) * interval * sampleValue(rightKey, component, 0);
        } else if (sampler.interpolation == Interpolation::linear) {
            result = glm::mix(result, sampleValue(rightKey, component), fraction);
        }

        if (!std::isfinite(result)) {
            throw std::runtime_error("Non-finite animation sample.");
        }

        sampled_[component] = result;
    }

    switch (channel.path) {
    case AnimationPath::translation:
        translations_[channel.node] = {sampled_[0], sampled_[1], sampled_[2]};
        break;
    case AnimationPath::scale:
        scales_[channel.node] = {sampled_[0], sampled_[1], sampled_[2]};
        break;
    case AnimationPath::rotation: {
        const glm::quat rotation{sampled_[3], sampled_[0], sampled_[1], sampled_[2]};

        if (glm::dot(rotation, rotation) < 1e-12F) {
            throw std::runtime_error("Animation produced a zero quaternion.");
        }

        rotations_[channel.node] = glm::normalize(rotation);
        break;
    }

    case AnimationPath::weights:
        pose_.weights[channel.node] = sampled_;
        break;
    }
}

void ModelInstance::updateNodeTransforms() {
    for (const auto nodeIndex : order_) {
        const auto& node = model_->nodes[nodeIndex];
        auto& localTransform = pose_.local[nodeIndex];
        localTransform = node.matrixTransform
                             ? node.localTransform
                             : glm::translate(glm::mat4{1}, translations_[nodeIndex]) *
                                   glm::mat4_cast(rotations_[nodeIndex]) * glm::scale(glm::mat4{1}, scales_[nodeIndex]);
        pose_.world[nodeIndex] = node.parent ? pose_.world[*node.parent] * localTransform : localTransform;

        if (!isFinite(pose_.world[nodeIndex])) {
            throw std::runtime_error("Non-finite animated world transform: " + node.name);
        }
    }
}

void ModelInstance::updateSkinMatrices() {
    for (const auto nodeIndex : order_) {
        const auto& node = model_->nodes[nodeIndex];

        if (!node.skin) {
            continue;
        }

        const auto& skin = model_->skins.at(*node.skin);
        const float determinant = glm::determinant(pose_.world[nodeIndex]);

        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12F) {
            throw std::runtime_error("Singular animated skinned mesh node.");
        }

        const auto inverseNode = glm::inverse(pose_.world[nodeIndex]);

        for (std::size_t jointIndex = 0; jointIndex < skin.joints.size(); ++jointIndex) {
            auto& jointMatrix = pose_.joints[nodeIndex][jointIndex];
            jointMatrix = node.bindPosePreview ? inverseNode * pose_.world[skin.joints.front()]
                                               : inverseNode * pose_.world[skin.joints[jointIndex]] *
                                                     skin.inverseBindMatrices[jointIndex];

            if (!isFinite(jointMatrix)) {
                throw std::runtime_error("Non-finite joint matrix in skin: " + skin.name);
            }
        }
    }
}

} // namespace grimlock::assets
