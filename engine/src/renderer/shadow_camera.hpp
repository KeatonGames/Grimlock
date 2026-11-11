#pragma once
#include "grimlock/camera.hpp"
#include "grimlock/lighting.hpp"
#include "grimlock/shadows.hpp"

namespace grimlock::renderer {
struct alignas(16) ShadowUniforms {
    glm::mat4 viewProjection{1};
    glm::vec4 map{0};  // enabled, inverse resolution, PCF radius, strength
    glm::vec4 bias{0}; // receiver depth bias, world normal bias, coverage radius
};

static_assert(sizeof(ShadowUniforms) == 96);
[[nodiscard]] ShadowUniforms shadowUniforms(const Camera& camera,
                                            float aspect,
                                            const DirectionalLight& sun,
                                            const ShadowSettings& settings,
                                            std::uint32_t resolution);
[[nodiscard]] bool shadowIntersects(const glm::mat4& matrix, glm::vec3 center, float radius);
} // namespace grimlock::renderer
