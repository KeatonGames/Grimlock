#include "renderer/mipmaps.hpp"

#include "grimlock/color.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace grimlock::renderer {
std::vector<assets::ImageData> rgbaMipChain(const assets::ImageData& base, bool srgb) {
    if (!base.width || !base.height || base.pixels.size() != static_cast<std::size_t>(base.width) * base.height * 4) {
        throw std::invalid_argument("Invalid RGBA mip source.");
    }
    std::vector<assets::ImageData> levels{base};
    while (levels.back().width > 1 || levels.back().height > 1) {
        const auto& previous = levels.back();
        assets::ImageData next;
        next.name = base.name;
        next.width = std::max(1U, previous.width / 2);
        next.height = std::max(1U, previous.height / 2);
        next.pixels.resize(static_cast<std::size_t>(next.width) * next.height * 4);
        for (std::uint32_t y = 0; y < next.height; ++y) {
            for (std::uint32_t x = 0; x < next.width; ++x) {
                const float x0 = float(x) * previous.width / next.width,
                            x1 = float(x + 1) * previous.width / next.width;
                const float y0 = float(y) * previous.height / next.height,
                            y1 = float(y + 1) * previous.height / next.height;
                for (int c = 0; c < 4; ++c) {
                    float sum = 0, weight = 0;
                    for (auto sy = static_cast<std::uint32_t>(y0);
                         sy < std::min(previous.height, static_cast<std::uint32_t>(std::ceil(y1)));
                         ++sy) {
                        for (auto sx = static_cast<std::uint32_t>(x0);
                             sx < std::min(previous.width, static_cast<std::uint32_t>(std::ceil(x1)));
                             ++sx) {
                            const float area = (std::min(x1, float(sx + 1)) - std::max(x0, float(sx))) *
                                               (std::min(y1, float(sy + 1)) - std::max(y0, float(sy)));
                            float value =
                                previous.pixels[(static_cast<std::size_t>(sy) * previous.width + sx) * 4 + c] / 255.0F;
                            if (srgb && c < 3) {
                                value = srgbToLinear(value);
                            }
                            sum += value * area;
                            weight += area;
                        }
                    }
                    float value = sum / weight;
                    if (srgb && c < 3) {
                        value = linearToSrgb(value);
                    }
                    next.pixels[(static_cast<std::size_t>(y) * next.width + x) * 4 + c] =
                        static_cast<std::uint8_t>(std::clamp(std::lround(value * 255), 0L, 255L));
                }
            }
        }
        levels.push_back(std::move(next));
    }
    return levels;
}
} // namespace grimlock::renderer
