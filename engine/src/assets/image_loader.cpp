#include "assets/image_loader.hpp"
#include "core/startup_profile.hpp"

#include <stb_image.h>

#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <cmath>

namespace grimlock::assets {

ImageData decodeImage(const std::span<const std::byte> encoded, const std::string& name) {
    diagnostics::StartupSpan profile{"texture.decode.PNG_JPEG"};
    const auto fail = [&name](const std::string& message) { return std::runtime_error("Image '" + name + "': " + message); };
    if (encoded.empty() || encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw fail("empty image or encoded size exceeds decoder limits");
    }
    int width = 0, height = 0, channels = 0;
    const auto* bytes = reinterpret_cast<const stbi_uc*>(encoded.data());
    const int size = static_cast<int>(encoded.size());
    if (stbi_is_hdr_from_memory(bytes, size) || !stbi_info_from_memory(bytes, size, &width, &height, &channels) || width <= 0 || height <= 0
        || width > STBI_MAX_DIMENSIONS || height > STBI_MAX_DIMENSIONS) {
        throw fail("invalid/unsupported PNG or JPEG, or dimensions exceed 16384");
    }
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded{
        stbi_load_from_memory(bytes, size, &width, &height, &channels, STBI_rgb_alpha), stbi_image_free};
    if (!decoded) { throw fail(std::string{"PNG/JPEG decoding failed: "} + stbi_failure_reason()); }
    const auto byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    return {name, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
        std::vector<std::uint8_t>{decoded.get(), decoded.get() + byteCount}};
}

ImageData loadImage(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { throw std::runtime_error("Cannot open image: " + path.string()); }
    const auto size = static_cast<std::streamoff>(file.tellg());
    if (size <= 0 || size > std::numeric_limits<int>::max()) { throw std::runtime_error("Invalid image size: " + path.string()); }
    std::vector<std::byte> encoded(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(encoded.data()), size)) { throw std::runtime_error("Cannot read image: " + path.string()); }
    return decodeImage(encoded, path.string());
}

FloatImageData loadLinearImage(const std::filesystem::path& path) {
    diagnostics::StartupSpan profile{"texture.read_decode.environment"};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { throw std::runtime_error("Cannot open environment image: " + path.string()); }
    const auto size = static_cast<std::streamoff>(file.tellg());
    if (size <= 0 || size > std::numeric_limits<int>::max()) { throw std::runtime_error("Invalid environment image size: " + path.string()); }
    std::vector<std::byte> encoded(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(encoded.data()), size)) { throw std::runtime_error("Cannot read environment image: " + path.string()); }
    const auto* bytes = reinterpret_cast<const stbi_uc*>(encoded.data());
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(bytes, static_cast<int>(size), &width, &height, &channels)
        || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        throw std::runtime_error("Invalid environment image or dimensions exceed 4096: " + path.string());
    }
    FloatImageData result{path.string(), static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
        std::vector<glm::vec4>(static_cast<std::size_t>(width) * height)};
    if (stbi_is_hdr_from_memory(bytes, static_cast<int>(size))) {
        std::unique_ptr<float, decltype(&stbi_image_free)> decoded{
            stbi_loadf_from_memory(bytes, static_cast<int>(size), &width, &height, &channels, 4), stbi_image_free};
        if (!decoded) { throw std::runtime_error("Cannot decode HDR environment: " + path.string()); }
        for (std::size_t i = 0; i < result.pixels.size(); ++i) {
            for (int c = 0; c < 3; ++c) {
                const float value = decoded.get()[i * 4 + c];
                if (!std::isfinite(value) || value < 0.0F) { throw std::runtime_error("Invalid HDR radiance: " + path.string()); }
                result.pixels[i][c] = value;
            }
            result.pixels[i].a = 1;
        }
    } else {
        const auto decoded = decodeImage(encoded, path.string());
        for (std::size_t i = 0; i < result.pixels.size(); ++i) {
            for (int c = 0; c < 3; ++c) {
                const float value = decoded.pixels[i * 4 + c] / 255.0F;
                result.pixels[i][c] = value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
            }
            result.pixels[i].a = 1;
        }
    }
    return result;
}

} // namespace grimlock::assets
