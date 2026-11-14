#pragma once

#include "grimlock/assets/image_data.hpp"

#include <vector>

namespace grimlock::renderer {
// CPU fallback for sampled RGBA8 formats without linear blit support. Alpha stays linear.
std::vector<assets::ImageData> rgbaMipChain(const assets::ImageData& base, bool srgb);
} // namespace grimlock::renderer
