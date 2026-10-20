#pragma once
#include "grimlock/camera.hpp"

#include <optional>

namespace grimlock {
struct Ray {
    glm::dvec3 origin{}, direction{0, 0, -1}; // Unit world direction; t is world distance.
    double minimum = 0, maximum = 0;
};

// Logical SDL cursor/window coordinates; drawable size supplies Vulkan's render aspect.
[[nodiscard]] std::optional<Ray> mouseRay(const Camera& camera, glm::vec2 cursor, glm::ivec2 window,
                                          glm::ivec2 drawable);
[[nodiscard]] std::optional<glm::dvec3> intersectPlaneXZ(const Ray& ray, double height) noexcept;
} // namespace grimlock
