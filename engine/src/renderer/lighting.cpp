#include "renderer/lighting.hpp"

#include <cmath>
#include <stdexcept>

namespace grimlock::renderer {
LightingUniforms lightingUniforms(const Lighting& lighting, const std::uint32_t specularMipLevels) {
    const auto finite = [](const glm::vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    const auto color = [&](const glm::vec3 value) {
        return finite(value) && glm::all(glm::greaterThanEqual(value, glm::vec3{0}));
    };
    const auto& directional = lighting.directional;
    const auto length = glm::dot(directional.direction, directional.direction);
    if (lighting.pointCount > maxPointLights || lighting.spotCount > maxPointLights ||
        !finite(directional.direction) || !std::isfinite(length) || length < 1.0e-12F ||
        !color(directional.color) || !std::isfinite(directional.intensity) ||
        directional.intensity < 0 || !std::isfinite(lighting.environmentIntensity) ||
        lighting.environmentIntensity < 0 || !std::isfinite(lighting.environmentRotation) ||
        !std::isfinite(lighting.exposure) || lighting.exposure <= 0 || specularMipLevels == 0) {
        throw std::invalid_argument("Invalid lighting configuration.");
    }
    LightingUniforms result;
    result.directionIntensity = {directional.direction / std::sqrt(length), directional.intensity};
    result.directionalColor = {directional.color, 0};
    result.environment = {lighting.environmentIntensity, lighting.environmentRotation,
                          static_cast<float>(specularMipLevels - 1), 0};
    result.counts.x = lighting.pointCount;
    result.counts.y = lighting.spotCount;
    for (std::uint32_t i = 0; i < lighting.pointCount; ++i) {
        const auto& point = lighting.points[i];
        if (!finite(point.position) || !color(point.color) || !std::isfinite(point.intensity) ||
            point.intensity < 0 || !std::isfinite(point.range) || point.range < 0) {
            throw std::invalid_argument("Invalid point light.");
        }
        result.points[i] = {{point.position, point.range}, {point.color, point.intensity}};
    }
    for (std::uint32_t i = 0; i < lighting.spotCount; ++i) {
        const auto& spot = lighting.spots[i];
        const float length2 = glm::dot(spot.direction, spot.direction);
        if (!finite(spot.position) || !color(spot.color) || !std::isfinite(spot.intensity) ||
            spot.intensity < 0 || !std::isfinite(spot.range) || spot.range < 0 ||
            !finite(spot.direction) || !std::isfinite(length2) || length2 < 1e-12F ||
            !std::isfinite(spot.innerCone) || !std::isfinite(spot.outerCone) ||
            spot.innerCone < 0 || spot.innerCone >= spot.outerCone ||
            spot.outerCone > 1.570796327F) {
            throw std::invalid_argument("Invalid spot light.");
        }
        result.spots[i] = {{spot.position, spot.range},
                           {spot.color, spot.intensity},
                           {spot.direction / std::sqrt(length2), std::cos(spot.innerCone)},
                           {std::cos(spot.outerCone), 0, 0, 0}};
    }
    return result;
}

void addModelLights(Lighting& lighting, const assets::ModelData& model,
                    const assets::ModelPose& pose, const glm::mat4& instance,
                    bool& directionalUsed) {
    for (const auto node : pose.activeNodes) {
        if (!model.nodes[node].light) {
            continue;
        }
        const auto& light = model.lights.at(*model.nodes[node].light);
        const auto world = instance * pose.world.at(node);
        const auto position = glm::vec3{world[3]},
                   direction = glm::vec3{world * glm::vec4{0, 0, -1, 0}};
        switch (light.type) {
        case assets::LightType::directional:
            if (directionalUsed) {
                throw std::runtime_error("Scene exceeds the one-directional-light limit.");
            }
            lighting.directional = {direction, light.intensity, light.color};
            directionalUsed = true;
            break;
        case assets::LightType::point:
            if (lighting.pointCount >= maxPointLights) {
                throw std::runtime_error("Scene exceeds eight point lights.");
            }
            lighting.points[lighting.pointCount++] = {position, light.intensity, light.color,
                                                      light.range};
            break;
        case assets::LightType::spot:
            if (lighting.spotCount >= maxPointLights) {
                throw std::runtime_error("Scene exceeds eight spot lights.");
            }
            lighting.spots[lighting.spotCount++] = {
                position,  light.intensity, light.color,    light.range,
                direction, light.innerCone, light.outerCone};
            break;
        }
    }
}
} // namespace grimlock::renderer

namespace grimlock {
void validateLighting(const Lighting& lighting) {
    (void)renderer::lightingUniforms(lighting, 1);
}
} // namespace grimlock
