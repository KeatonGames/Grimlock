#include "grimlock/assets/animation.hpp"

#include "core/startup_profile.hpp"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <stdexcept>
#include <utility>

namespace grimlock::assets {
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
        time_ = clip.start + static_cast<float>(std::fmod(
                                 std::fmod(next - clip.start, duration) + duration, duration));
    } else {
        time_ = static_cast<float>(
            std::clamp(next, static_cast<double>(clip.start), static_cast<double>(clip.end)));
        if ((speed_ >= 0 && next >= clip.end) || (speed_ < 0 && next <= clip.start)) {
            playing_ = false;
        }
    }
    evaluate(true);
}

void ModelInstance::evaluate(bool animated) {
    animatedPose_ = animated;
    for (std::size_t n = 0; n < model_->nodes.size(); ++n) {
        const auto& node = model_->nodes[n];
        translations_[n] = node.translation;
        rotations_[n] = node.rotation;
        scales_[n] = node.scale;
        pose_.weights[n] = node.weights;
    }
    if (animated) {
        const auto& clip = model_->animations.at(clip_);
        for (const auto& channel : clip.channels) {
            const auto& s = clip.samplers[channel.sampler];
            const auto components = s.components;
            const auto right = std::upper_bound(s.times.begin(), s.times.end(), time_);
            const std::size_t a = right == s.times.begin()
                                      ? 0
                                      : static_cast<std::size_t>(right - s.times.begin() - 1);
            const std::size_t b = std::min(a + 1, s.times.size() - 1);
            const float dt = s.times[b] - s.times[a],
                        t = dt > 0 ? std::clamp((time_ - s.times[a]) / dt, 0.F, 1.F) : 0;
            const bool cubic = s.interpolation == Interpolation::cubicSpline;
            const auto value = [&](std::size_t key, std::size_t component, std::size_t part = 1) {
                return s
                    .values[(key * (cubic ? 3 : 1) + (cubic ? part : 0)) * components + component];
            };
            sampled_.resize(components);
            if (channel.path == AnimationPath::rotation &&
                s.interpolation == Interpolation::linear) {
                const glm::quat q0{value(a, 3), value(a, 0), value(a, 1), value(a, 2)},
                    q1{value(b, 3), value(b, 0), value(b, 1), value(b, 2)};
                rotations_[channel.node] = glm::normalize(glm::slerp(q0, q1, t));
                continue;
            }
            for (std::size_t c = 0; c < components; ++c) {
                float result = value(a, c);
                if (cubic && a != b) {
                    const float t2 = t * t, t3 = t2 * t;
                    result = (2 * t3 - 3 * t2 + 1) * value(a, c) +
                             (t3 - 2 * t2 + t) * dt * value(a, c, 2) +
                             (-2 * t3 + 3 * t2) * value(b, c) + (t3 - t2) * dt * value(b, c, 0);
                } else if (s.interpolation == Interpolation::linear) {
                    result = glm::mix(result, value(b, c), t);
                }
                if (!std::isfinite(result)) {
                    throw std::runtime_error("Non-finite animation sample.");
                }
                sampled_[c] = result;
            }
            switch (channel.path) {
            case AnimationPath::translation:
                translations_[channel.node] = {sampled_[0], sampled_[1], sampled_[2]};
                break;
            case AnimationPath::scale:
                scales_[channel.node] = {sampled_[0], sampled_[1], sampled_[2]};
                break;
            case AnimationPath::rotation: {
                glm::quat q{sampled_[3], sampled_[0], sampled_[1], sampled_[2]};
                if (glm::dot(q, q) < 1e-12F) {
                    throw std::runtime_error("Animation produced a zero quaternion.");
                }
                rotations_[channel.node] = glm::normalize(q);
                break;
            }
            case AnimationPath::weights:
                pose_.weights[channel.node] = sampled_;
                break;
            }
        }
    }
    for (const auto n : order_) {
        const auto& node = model_->nodes[n];
        pose_.local[n] = node.matrixTransform ? node.localTransform
                                              : glm::translate(glm::mat4{1}, translations_[n]) *
                                                    glm::mat4_cast(rotations_[n]) *
                                                    glm::scale(glm::mat4{1}, scales_[n]);
        pose_.world[n] = node.parent ? pose_.world[*node.parent] * pose_.local[n] : pose_.local[n];
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (!std::isfinite(pose_.world[n][c][r])) {
                    throw std::runtime_error("Non-finite animated world transform: " + node.name);
                }
            }
        }
    }
    for (const auto n : order_) {
        const auto& node = model_->nodes[n];
        if (!node.skin) {
            continue;
        }
        const auto& skin = model_->skins.at(*node.skin);
        const float determinant = glm::determinant(pose_.world[n]);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12F) {
            throw std::runtime_error("Singular animated skinned mesh node.");
        }
        const auto inverseNode = glm::inverse(pose_.world[n]);
        for (std::size_t j = 0; j < skin.joints.size(); ++j) {
            pose_.joints[n][j] =
                node.bindPosePreview
                    ? inverseNode * pose_.world[skin.joints.front()]
                    : inverseNode * pose_.world[skin.joints[j]] * skin.inverseBindMatrices[j];
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    if (!std::isfinite(pose_.joints[n][j][c][r])) {
                        throw std::runtime_error("Non-finite joint matrix in skin: " + skin.name);
                    }
                }
            }
        }
    }
}
} // namespace grimlock::assets
