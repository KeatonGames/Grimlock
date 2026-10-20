#include "grimlock/camera.hpp"

#include "grimlock/ray.hpp"

#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
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
    if (!finite(position) || !finite(target) || !finite(up) || !std::isfinite(directionLength) ||
        !std::isfinite(sideLength) || directionLength < 1.0e-12F || sideLength < 1.0e-12F) {
        throw std::invalid_argument(
            "Camera position/target/up must define a finite, non-degenerate view.");
    }
    return glm::lookAtRH(position, target, up);
}

glm::mat4 Camera::projection(const float aspectRatio) const {
    if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0F || !std::isfinite(verticalFovDegrees) ||
        verticalFovDegrees <= 0.0F || verticalFovDegrees >= 179.0F || !std::isfinite(nearPlane) ||
        !std::isfinite(farPlane) || nearPlane <= 0.0F || farPlane <= nearPlane) {
        throw std::invalid_argument("Invalid camera perspective parameters.");
    }
    auto result =
        glm::perspectiveRH_ZO(glm::radians(verticalFovDegrees), aspectRatio, nearPlane, farPlane);
    result[1][1] *= -1.0F; // Vulkan framebuffer Y; depth is explicitly zero-to-one.
    return result;
}

std::optional<Ray> mouseRay(const Camera& camera, glm::vec2 cursor, glm::ivec2 window,
                            glm::ivec2 drawable) {
    if (window.x <= 0 || window.y <= 0 || drawable.x <= 0 || drawable.y <= 0 ||
        !std::isfinite(cursor.x) || !std::isfinite(cursor.y) || cursor.x < 0 || cursor.y < 0 ||
        cursor.x >= window.x || cursor.y >= window.y) {
        return {};
    }
    // Positive-height Vulkan viewport: Camera::projection already flips Y.
    const glm::dvec2 ndc = 2.0 * glm::dvec2{cursor} / glm::dvec2{window} - 1.0;
    const auto inverse =
        glm::inverse(glm::dmat4{camera.projection(static_cast<float>(drawable.x) / drawable.y)} *
                     glm::dmat4{camera.view()});
    auto nearPoint = inverse * glm::dvec4{ndc, 0, 1}, farPoint = inverse * glm::dvec4{ndc, 1, 1};
    if (nearPoint.w == 0 || farPoint.w == 0) {
        return {};
    }
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    Ray ray;
    ray.origin = camera.position;
    ray.direction = glm::normalize(glm::dvec3{farPoint - nearPoint});
    ray.minimum = glm::dot(glm::dvec3{nearPoint} - ray.origin, ray.direction);
    ray.maximum = glm::dot(glm::dvec3{farPoint} - ray.origin, ray.direction);
    if (!std::isfinite(ray.direction.x) || !std::isfinite(ray.direction.y) ||
        !std::isfinite(ray.direction.z) || !std::isfinite(ray.minimum) ||
        !std::isfinite(ray.maximum) || ray.maximum <= ray.minimum) {
        return {};
    }
    return ray;
}

std::optional<glm::dvec3> intersectPlaneXZ(const Ray& ray, double height) noexcept {
    if (!std::isfinite(height) || !std::isfinite(ray.direction.y) ||
        std::abs(ray.direction.y) < 1e-12 || !std::isfinite(ray.minimum) ||
        !std::isfinite(ray.maximum) || ray.maximum < ray.minimum) {
        return {};
    }
    const double distance = (height - ray.origin.y) / ray.direction.y;
    if (!std::isfinite(distance) || distance < ray.minimum || distance > ray.maximum) {
        return {};
    }
    const auto point = ray.origin + distance * ray.direction;
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
        return {};
    }
    return point;
}
} // namespace grimlock
