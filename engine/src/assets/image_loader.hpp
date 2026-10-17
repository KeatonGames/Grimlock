#pragma once
#include "grimlock/assets/image_data.hpp"
#include <cstddef>
#include <filesystem>
#include <span>

namespace grimlock::assets {
[[nodiscard]] ImageData decodeImage(std::span<const std::byte> encoded, const std::string& name);
[[nodiscard]] ImageData loadImage(const std::filesystem::path& path);
// Radiance HDR is read linearly; PNG/JPEG RGB is explicitly decoded from sRGB.
[[nodiscard]] FloatImageData loadLinearImage(const std::filesystem::path& path);

} // namespace grimlock::assets
