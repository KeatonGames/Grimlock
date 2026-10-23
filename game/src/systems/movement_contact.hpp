#pragma once

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace grimlock::game {

inline double contactFraction(glm::dvec2 offset, glm::dvec2 displacement, double distance) {
    const double a = glm::dot(displacement, displacement);
    const double b = glm::dot(offset, displacement);
    const double c = glm::dot(offset, offset) - distance * distance;
    if (a < 1e-20 || b >= 0) {
        return 1;
    }
    if (c <= 0) {
        return 0;
    }
    const double discriminant = b * b - a * c;
    return discriminant <= 0 ? 1 : std::clamp((-b - std::sqrt(discriminant)) / a - 1e-6, 0.0, 1.0);
}

} // namespace grimlock::game
