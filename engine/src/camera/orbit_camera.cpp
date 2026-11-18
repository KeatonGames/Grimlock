#include "grimlock/orbit_camera.hpp"

#include <SDL3/SDL_scancode.h>
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <numbers>
#include <stdexcept>

namespace grimlock {

namespace {

constexpr double pi = std::numbers::pi;

bool finite(glm::vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

double groundPitch(const OrbitCameraConfig& c) {
    return std::asin(std::clamp(-static_cast<double>(c.target.y) / c.distance, 0.0, 1.0));
}

double minimumDistance(const OrbitCameraConfig& c) {
    const double sine = std::sin(c.pitch);

    return std::max(static_cast<double>(c.minDistance), c.target.y < 0 ? -static_cast<double>(c.target.y) / sine : 0.0);
}

float wrap(double angle) {
    return static_cast<float>(std::remainder(angle, 2 * pi));
}

} // namespace

OrbitCameraController::OrbitCameraController(const OrbitCameraConfig& config) {
    configure(config);
}

void OrbitCameraController::configure(const OrbitCameraConfig& value) {
    if (!finite(value.target)) {
        throw std::invalid_argument("Orbit target must be finite.");
    }

    for (const auto v : {value.distance,
                         value.yaw,
                         value.pitch,
                         value.panSensitivity,
                         value.keyboardSpeed,
                         value.panDragThreshold,
                         value.rotationSensitivity,
                         value.zoomSensitivity,
                         value.minDistance,
                         value.maxDistance,
                         value.minPitch,
                         value.maxPitch}) {
        if (!std::isfinite(v)) {
            throw std::invalid_argument("Orbit configuration must be finite.");
        }
    }

    if (value.minDistance <= 0 || value.maxDistance < value.minDistance || value.minPitch < 0 ||
        value.maxPitch >= pi / 2 || value.maxPitch < value.minPitch || value.panSensitivity < 0 ||
        value.rotationSensitivity < 0 || value.zoomSensitivity < 0 || value.panDragThreshold < 0 ||
        value.keyboardSpeed < 0 || value.target.y + value.maxDistance * std::sin(value.maxPitch) < 0) {
        throw std::invalid_argument(
            "Invalid orbit sensitivities/bounds or target unreachable above the Y=0 ground plane.");
    }

    auto next = value;
    if (value.keyboardMovement != config_.keyboardMovement || value.strategyControls != config_.strategyControls) {
        keyboardVelocity_ = {0, 0};
    }
    if (value.panHalfExtent) {
        const auto extent = *value.panHalfExtent;
        if (!finite(glm::vec3{extent, 0}) || extent.x <= 0 || extent.y <= 0) {
            throw std::invalid_argument("Invalid pan bounds");
        }
        next.target.x = std::clamp(next.target.x, -extent.x, extent.x);
        next.target.z = std::clamp(next.target.z, -extent.y, extent.y);
    }
    next.yaw = wrap(next.yaw);
    next.distance = std::clamp(next.distance, next.minDistance, next.maxDistance);

    // A target below ground (the supplied scene uses y=-0.4) needs a higher
    // pitch/radius floor. Orbit keeps distance; zoom keeps orientation.
    if (next.target.y < 0) {
        next.distance = std::max(next.distance, static_cast<float>(-next.target.y / std::sin(next.maxPitch)));
    }

    next.pitch = std::clamp(next.pitch, next.minPitch, next.maxPitch);
    next.pitch = std::min(next.maxPitch, std::max(next.pitch, static_cast<float>(groundPitch(next))));
    config_ = next;
    cancelDrag();
}

void OrbitCameraController::synchronize(Camera& camera) {
    (void)camera.view();
    (void)camera.projection(1);
    const glm::dvec3 offset = glm::dvec3{camera.position} - glm::dvec3{camera.target};
    auto next = config_;
    next.target = camera.target;
    next.distance = static_cast<float>(glm::length(offset));
    next.yaw = static_cast<float>(std::atan2(offset.x, offset.z));
    next.pitch = static_cast<float>(std::asin(std::clamp(offset.y / glm::length(offset), -1.0, 1.0)));
    const auto distance = next.distance, pitch = next.pitch;
    configure(next);
    keyboardVelocity_ = {0, 0};

    if (distance != config_.distance || pitch != config_.pitch || camera.target != config_.target ||
        camera.up != glm::vec3{0, 1, 0}) {
        apply(camera);
    }
}

void OrbitCameraController::apply(Camera& camera) const {
    const double cp = std::cos(config_.pitch), sp = std::sin(config_.pitch);
    const glm::dvec3 offset{cp * std::sin(config_.yaw), sp, cp * std::cos(config_.yaw)};
    auto next = camera;
    next.target = config_.target;
    next.position = glm::vec3{glm::dvec3{config_.target} + offset * static_cast<double>(config_.distance)};
    next.position.y = std::max(next.position.y, 0.0F); // Only floating-point roundoff at the ground boundary.
    next.up = {0, 1, 0};
    (void)next.view();
    (void)next.projection(1);
    camera = next;
}

void OrbitCameraController::cancelDrag() noexcept {
    drag_ = Drag::none;
}

void OrbitCameraController::follow(Camera& camera, glm::vec3 target, double delta) {
    if (!finite(target) || !std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Invalid camera follow input");
    }
    if (config_.panHalfExtent) {
        target.x = std::clamp(target.x, -config_.panHalfExtent->x, config_.panHalfExtent->x);
        target.z = std::clamp(target.z, -config_.panHalfExtent->y, config_.panHalfExtent->y);
    }
    config_.target = glm::mix(config_.target, target, static_cast<float>(1 - std::exp(-6 * delta)));
    apply(camera);
}

bool OrbitCameraController::update(Camera& camera, const InputState& input, float windowHeight, double delta) {
    if (!std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Invalid camera delta");
    }
    if (!input.focused || !std::isfinite(windowHeight) || windowHeight <= 0) {
        cancelDrag();
        keyboardVelocity_ = {0, 0};

        return false;
    }

    const auto action = config_.strategyControls ? (input.button(MouseButton::right).held ? Drag::orbit : Drag::none)
                        : input.button(MouseButton::middle).held ? Drag::orbit
                        : input.button(MouseButton::right).held  ? Drag::pan
                                                                 : Drag::none;
    const auto& button = input.button(action == Drag::orbit ? MouseButton::middle : MouseButton::right);
    const bool continuing = action == drag_ && !button.pressed && !button.released;
    drag_ = action;
    bool changed = false, rebuild = false;
    if (config_.strategyControls && config_.keyboardMovement) {
        if (input.keyboardCaptured) {
            keyboardVelocity_ = {0, 0};
        }
        glm::vec2 axes{static_cast<float>(input.key(SDL_SCANCODE_D).held) - input.key(SDL_SCANCODE_A).held,
                       static_cast<float>(input.key(SDL_SCANCODE_W).held) - input.key(SDL_SCANCODE_S).held};
        if (input.keyboardCaptured) {
            axes = {0, 0};
        }
        if (glm::length(axes) > 1) {
            axes = glm::normalize(axes);
        }
        keyboardVelocity_ = glm::mix(keyboardVelocity_, axes, static_cast<float>(1 - std::exp(-12 * delta)));
        if (glm::length(keyboardVelocity_) > 1e-4F) {
            axes = keyboardVelocity_;
            const glm::vec3 forward{-std::sin(config_.yaw), 0, -std::cos(config_.yaw)};
            const glm::vec3 right{std::cos(config_.yaw), 0, -std::sin(config_.yaw)};
            config_.target += (right * axes.x + forward * axes.y) * config_.keyboardSpeed * static_cast<float>(delta);
            if (config_.panHalfExtent) {
                config_.target.x = std::clamp(config_.target.x, -config_.panHalfExtent->x, config_.panHalfExtent->x);
                config_.target.z = std::clamp(config_.target.z, -config_.panHalfExtent->y, config_.panHalfExtent->y);
            }
            changed = rebuild = true;
        }
    }
    const auto motion = input.mouseMotion;

    if (continuing && std::isfinite(motion.x) && std::isfinite(motion.y) && (motion.x != 0 || motion.y != 0)) {
        if (action == Drag::orbit && config_.rotationSensitivity > 0) {
            config_.yaw = wrap(config_.yaw - static_cast<double>(motion.x) * config_.rotationSensitivity);

            const auto low = std::min(static_cast<double>(config_.maxPitch),
                                      std::max(static_cast<double>(config_.minPitch), groundPitch(config_)));
            config_.pitch = static_cast<float>(
                std::clamp(config_.pitch + static_cast<double>(motion.y) * config_.rotationSensitivity,
                           low,
                           static_cast<double>(config_.maxPitch)));
            changed = rebuild = true;
        } else if (action == Drag::pan && config_.panSensitivity > 0 &&
                   (config_.panDragThreshold == 0 || input.rightDragDistance > config_.panDragThreshold)) {
            (void)camera.projection(1);

            const auto forward = glm::normalize(glm::dvec3{camera.target} - glm::dvec3{camera.position});
            const auto right = glm::normalize(glm::cross(forward, glm::dvec3{0, 1, 0}));
            auto up = glm::cross(right, forward);
            up.y = 0;
            up = glm::dot(up, up) > 1e-16 ? glm::normalize(up)
                                          : glm::dvec3{-std::sin(config_.yaw), 0, -std::cos(config_.yaw)};

            const double scale = 2 * config_.distance *
                                 std::tan(glm::radians(static_cast<double>(camera.verticalFovDegrees)) / 2) /
                                 windowHeight * config_.panSensitivity;
            const auto target = glm::dvec3{config_.target} +
                                (-static_cast<double>(motion.x) * right + static_cast<double>(motion.y) * up) * scale;
            auto next = glm::vec3{target};
            if (config_.panHalfExtent) {
                next.x = std::clamp(next.x, -config_.panHalfExtent->x, config_.panHalfExtent->x);
                next.z = std::clamp(next.z, -config_.panHalfExtent->y, config_.panHalfExtent->y);
            }

            if (!finite(next)) {
                throw std::overflow_error("Orbit pan exceeds finite coordinates.");
            }

            const auto translation = next - camera.target;
            camera.position += translation;
            camera.target = next;
            config_.target = next;
            changed = true;
        }
    }

    if (std::isfinite(input.mouseWheel.y) && input.mouseWheel.y != 0 && config_.zoomSensitivity > 0) {
        const double low = std::min(minimumDistance(config_), static_cast<double>(config_.maxDistance));
        const double value =
            std::log(config_.distance) - static_cast<double>(input.mouseWheel.y) * config_.zoomSensitivity;
        config_.distance = static_cast<float>(
            std::exp(std::clamp(value, std::log(low), std::log(static_cast<double>(config_.maxDistance)))));
        changed = rebuild = true;
    }

    if (rebuild) {
        apply(camera);
    }

    return changed;
}

} // namespace grimlock
