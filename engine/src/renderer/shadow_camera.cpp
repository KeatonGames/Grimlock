#include "renderer/shadow_camera.hpp"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <stdexcept>

namespace grimlock {
void validateShadowSettings(const ShadowSettings& s) {
    if ((s.quality != ShadowQuality::off && s.quality != ShadowQuality::low && s.quality != ShadowQuality::medium &&
         s.quality != ShadowQuality::high) ||
        s.pcfRadius < 0 || s.pcfRadius > 2 || !std::isfinite(s.strength) || s.strength < 0 || s.strength > 1 ||
        !std::isfinite(s.depthBias) || s.depthBias < 0 || s.depthBias > 0.01F || !std::isfinite(s.normalBias) ||
        s.normalBias < 0 || s.normalBias > 1 || !std::isfinite(s.constantBias) || s.constantBias < 0 ||
        s.constantBias > 16 || !std::isfinite(s.slopeBias) || s.slopeBias < 0 || s.slopeBias > 16 ||
        !std::isfinite(s.maxDistance) || s.maxDistance < 8 || s.maxDistance > 500) {
        throw std::invalid_argument("Invalid directional shadow settings.");
    }
}
} // namespace grimlock

namespace grimlock::renderer {
ShadowUniforms shadowUniforms(const Camera& camera,
                              float aspect,
                              const DirectionalLight& sun,
                              const ShadowSettings& settings,
                              std::uint32_t resolution) {
    validateShadowSettings(settings);
    ShadowUniforms result;
    if (!resolution || !sun.enabled || sun.intensity <= 0 || settings.strength <= 0) {
        return result;
    }
    const float distance = glm::length(camera.position - camera.target);
    // A rotation-invariant sphere avoids refitting the map on every orbit. Quantized
    // extent and light-space texel snapping stabilize panning and gradual zoom.
    const float visible = distance * std::tan(glm::radians(camera.verticalFovDegrees) * 0.5F) * std::max(aspect, 1.0F);
    const float radius = std::min(settings.maxDistance, std::max(8.0F, std::ceil((visible + 8) / 4) * 4));
    const auto direction = glm::normalize(sun.direction);
    const auto hint = std::abs(direction.y) > 0.95F ? glm::vec3{0, 0, 1} : glm::vec3{0, 1, 0};
    const auto right = glm::normalize(glm::cross(direction, hint));
    const auto up = glm::normalize(glm::cross(right, direction));
    const float texel = 2 * radius / resolution;
    auto center = camera.target;
    center += right * (std::round(glm::dot(center, right) / texel) * texel - glm::dot(center, right));
    center += up * (std::round(glm::dot(center, up) / texel) * texel - glm::dot(center, up));
    const auto view = glm::lookAtRH(center - direction * (radius * 2), center, up);
    auto projection = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.1F, radius * 4);
    projection[1][1] *= -1;
    result.viewProjection = projection * view;
    result.map = {1, 1.0F / resolution, static_cast<float>(settings.pcfRadius), settings.strength};
    result.bias = {settings.depthBias, settings.normalBias, radius, 0};
    return result;
}

bool shadowIntersects(const glm::mat4& matrix, glm::vec3 center, float radius) {
    const auto p = matrix * glm::vec4{center, 1};
    for (int axis = 0; axis < 3; ++axis) {
        const float r = radius * glm::length(glm::vec3{matrix[0][axis], matrix[1][axis], matrix[2][axis]});
        if (p[axis] < (axis == 2 ? 0.0F : -p.w) - r || p[axis] > p.w + r) {
            return false;
        }
    }
    return true;
}
} // namespace grimlock::renderer
