#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace grimlock::assets {
// Top-left origin, tightly packed RGBA8. Color space belongs to the texture's
// usage, not to its decoded bytes (base color is sRGB; data textures are linear).
struct ImageData {
    std::string name;
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
};

struct FloatImageData {
    std::string name;
    std::uint32_t width = 0, height = 0;
    std::vector<glm::vec4> pixels; // Linear-light RGBA; HDR RGB is not clamped to 1.
};

} // namespace grimlock::assets
