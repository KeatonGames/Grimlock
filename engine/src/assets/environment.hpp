#pragma once

#include "grimlock/assets/image_data.hpp"
#include <array>
#include <filesystem>

namespace grimlock::assets {

// Vulkan cube order: +X, -X, +Y, -Y, +Z, -Z; each face is top-left origin.
using EnvironmentCube = std::array<FloatImageData, 6>;
struct EnvironmentMaps {
    std::array<FloatImageData, 6> irradiance;
    std::vector<FloatImageData> specular; // mip-major, then six faces per mip
    FloatImageData brdf;
};

[[nodiscard]] EnvironmentCube loadEnvironmentCube(const std::filesystem::path& folder);
[[nodiscard]] EnvironmentCube fallbackEnvironment();
// Startup-only cosine convolution, GGX importance prefilter, and split-sum LUT.
[[nodiscard]] EnvironmentMaps preprocessEnvironment(const EnvironmentCube& source);
[[nodiscard]] glm::vec3 cubeDirection(std::size_t face, float u, float v);

} // namespace grimlock::assets
