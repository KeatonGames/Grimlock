#include "grimlock/camera.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace grimlock {

glm::mat4 Camera::view() const {
    const auto finite = [](const glm::vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    const glm::vec3 direction = target - position;
    const glm::vec3 side = glm::cross(direction, up);
    const float directionLength = glm::dot(direction, direction);
    const float sideLength = glm::dot(side, side);
    if (!finite(position) || !finite(target) || !finite(up)
        || !std::isfinite(directionLength) || !std::isfinite(sideLength)
        || directionLength < 1.0e-12F || sideLength < 1.0e-12F) {
        throw std::invalid_argument("Camera position/target/up must define a finite, non-degenerate view.");
    }
    return glm::lookAtRH(position, target, up);
}

glm::mat4 Camera::projection(const float aspectRatio) const {
    if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0F
        || !std::isfinite(verticalFovDegrees) || verticalFovDegrees <= 0.0F || verticalFovDegrees >= 179.0F
        || !std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0.0F || farPlane <= nearPlane) {
        throw std::invalid_argument("Invalid camera perspective parameters.");
    }
    auto result = glm::perspectiveRH_ZO(glm::radians(verticalFovDegrees), aspectRatio, nearPlane, farPlane);
    result[1][1] *= -1.0F; // Vulkan framebuffer Y; depth is explicitly zero-to-one.
    return result;
}

} // namespace grimlock
