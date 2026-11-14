#pragma once

#include <cstdint>

namespace grimlock {
enum class TextureFiltering {
    bilinear,
    trilinear,
    anisotropic
};
enum class ToneMapping {
    linear,
    reinhard,
    filmic
};

struct RenderSettings {
    TextureFiltering textureFiltering = TextureFiltering::anisotropic;
    std::uint32_t anisotropy = 8;
    ToneMapping toneMapping = ToneMapping::filmic;
    bool operator==(const RenderSettings&) const = default;
};

void validateRenderSettings(const RenderSettings& settings);
} // namespace grimlock
