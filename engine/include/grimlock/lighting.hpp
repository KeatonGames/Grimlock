#pragma once
#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace grimlock {
inline constexpr std::uint32_t maxPointLights = 8;

struct DirectionalLight {
    glm::vec3 direction{-0.4F, -0.8F, -0.5F}; // Direction light travels.
    float intensity = 3.0F;
    glm::vec3 color{1.0F}; // Linear RGB.
};

struct PointLight {
    glm::vec3 position{};
    float intensity = 20.0F; // Inverse-square intensity (candela-like scene units).
    glm::vec3 color{1.0F};
    float range = 0.0F; // Zero = infinite; positive = smooth cutoff distance.
};

struct SpotLight {
    glm::vec3 position{};
    float intensity = 20;
    glm::vec3 color{1};
    float range = 0;
    glm::vec3 direction{0, 0, -1};
    float innerCone = 0, outerCone = 0.785398163F;
};

struct Lighting {
    DirectionalLight directional;
    std::array<PointLight, maxPointLights> points{};
    std::uint32_t pointCount = 0;
    std::array<SpotLight, maxPointLights> spots{};
    std::uint32_t spotCount = 0;
    float environmentIntensity = 1.0F;
    float environmentRotation = 0.0F; // Yaw radians.
    float exposure = 1.0F;
};

// CPU validation shared by scene loading and the existing uniform packing path.
void validateLighting(const Lighting& lighting);

} // namespace grimlock
