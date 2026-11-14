#include "grimlock/render_settings.hpp"

#include "renderer/vulkan_renderer.hpp"

#include <stdexcept>

namespace grimlock {
void validateRenderSettings(const RenderSettings& settings) {
    if ((settings.textureFiltering != TextureFiltering::bilinear &&
         settings.textureFiltering != TextureFiltering::trilinear &&
         settings.textureFiltering != TextureFiltering::anisotropic) ||
        (settings.toneMapping != ToneMapping::linear && settings.toneMapping != ToneMapping::reinhard &&
         settings.toneMapping != ToneMapping::filmic) ||
        (settings.anisotropy != 1 && settings.anisotropy != 2 && settings.anisotropy != 4 && settings.anisotropy != 8 &&
         settings.anisotropy != 16)) {
        throw std::invalid_argument("Invalid rendering settings.");
    }
}
} // namespace grimlock

namespace grimlock::renderer {
std::vector<std::uint32_t> VulkanRenderer::supportedAnisotropy() const {
    std::vector<std::uint32_t> values{1};
    for (std::uint32_t value : {2U, 4U, 8U, 16U}) {
        if (value <= context_.maxAnisotropy()) {
            values.push_back(value);
        }
    }
    return values;
}

void VulkanRenderer::setRenderSettings(const RenderSettings& settings) {
    validateRenderSettings(settings);
    auto effective = settings;
    effective.anisotropy = 1;
    for (auto value : supportedAnisotropy()) {
        if (value <= settings.anisotropy) {
            effective.anisotropy = value;
        }
    }
    if (context_.maxAnisotropy() <= 1 && effective.textureFiltering == TextureFiltering::anisotropic) {
        effective.textureFiltering = TextureFiltering::trilinear;
    }
    filteringDirty_ |= effective.textureFiltering != renderSettings_.textureFiltering ||
                       effective.anisotropy != renderSettings_.anisotropy;
    renderSettings_ = effective;
}
} // namespace grimlock::renderer
