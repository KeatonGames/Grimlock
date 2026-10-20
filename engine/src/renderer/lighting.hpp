#pragma once

#include "grimlock/assets/animation.hpp"
#include "grimlock/lighting.hpp"

#include <cstddef>

namespace grimlock::renderer {

struct alignas(16) LightingUniforms {
    glm::vec4 directionIntensity{}, directionalColor{};
    glm::vec4 environment{}; // intensity, rotation, max specular LOD
    glm::uvec4 counts{};

    struct Point {
        glm::vec4 positionRange{}, colorIntensity{};
    };

    std::array<Point, maxPointLights> points{};

    struct Spot {
        glm::vec4 positionRange{}, colorIntensity{}, directionInner{}, outer{};
    };

    std::array<Spot, maxPointLights> spots{};
};

static_assert(sizeof(LightingUniforms) == 832);
static_assert(offsetof(LightingUniforms, spots) == 320);
[[nodiscard]] LightingUniforms lightingUniforms(const Lighting& lighting, std::uint32_t specularMipLevels);

// Append active scene lights only. Caller clears demo lights once when imported lights are present.
void addModelLights(Lighting& lighting,
                    const assets::ModelData& model,
                    const assets::ModelPose& pose,
                    const glm::mat4& instance,
                    bool& directionalUsed);

} // namespace grimlock::renderer
