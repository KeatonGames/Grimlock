#pragma once

#include <cmath>
#include <glm/glm.hpp>

namespace grimlock {
inline float srgbToLinear(float value) {
    return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

inline float linearToSrgb(float value) {
    return value <= 0.0031308F ? value * 12.92F : 1.055F * std::pow(value, 1 / 2.4F) - 0.055F;
}

inline glm::vec3 srgbToLinear(glm::vec3 value) {
    return {srgbToLinear(value.r), srgbToLinear(value.g), srgbToLinear(value.b)};
}

inline glm::vec3 linearToSrgb(glm::vec3 value) {
    return {linearToSrgb(value.r), linearToSrgb(value.g), linearToSrgb(value.b)};
}
} // namespace grimlock
