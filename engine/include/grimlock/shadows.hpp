#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace grimlock {
enum class ShadowQuality : std::uint32_t {
    off = 0,
    low = 1024,
    medium = 2048,
    high = 4096
};

struct ShadowSettings {
    ShadowQuality quality = ShadowQuality::medium;
    int pcfRadius = 1; // 0 = one comparison, 1 = 3x3, 2 = 5x5.
    float strength = 0.8F;
    float depthBias = 0.00012F;
    float normalBias = 0.01F;                    // World units, scaled down on surfaces facing the sun.
    float constantBias = 1.0F, slopeBias = 1.5F; // Vulkan raster depth bias.
    float maxDistance = 80;
};

struct ShadowProperties {
    bool cast = true, receive = true;
};

struct ShadowDebugInfo {
    glm::mat4 viewProjection{1};
    std::uint32_t resolution = 0, casterDraws = 0;
    float coverageRadius = 0;
    bool active = false;
    bool readbackAvailable = false;
};

void validateShadowSettings(const ShadowSettings& settings);
} // namespace grimlock
