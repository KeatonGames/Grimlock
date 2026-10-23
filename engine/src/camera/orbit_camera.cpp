#include "grimlock/orbit_camera.hpp"

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
        value.target.y + value.maxDistance * std::sin(value.maxPitch) < 0) {
        throw std::invalid_argument(
            "Invalid orbit sensitivities/bounds or target unreachable above the Y=0 ground plane.");
    }

    auto next = value;
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

    if (distance != config_.distance || pitch != config_.pitch || camera.up != glm::vec3{0, 1, 0}) {
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

bool OrbitCameraController::update(Camera& camera, const InputState& input, float windowHeight) {
    if (!input.focused || !std::isfinite(windowHeight) || windowHeight <= 0) {
        cancelDrag();

        return false;
    }

    const auto action = input.button(MouseButton::middle).held  ? Drag::orbit
                        : input.button(MouseButton::right).held ? Drag::pan
                                                                : Drag::none;
    const auto& button = input.button(action == Drag::orbit ? MouseButton::middle : MouseButton::right);
    const bool continuing = action == drag_ && !button.pressed && !button.released;
    drag_ = action;
    bool changed = false, rebuild = false;
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
            const auto next = glm::vec3{target};

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
