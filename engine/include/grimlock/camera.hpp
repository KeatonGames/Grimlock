#pragma once

#include <glm/glm.hpp>

namespace grimlock {

// Target and up specify orientation; geometry and camera state remain independent.
struct Camera {
    glm::vec3 position{3.0F, 2.2F, 4.0F};
    glm::vec3 target{0.0F};
    glm::vec3 up{0.0F, 1.0F, 0.0F};
    float verticalFovDegrees = 45.0F;
    float nearPlane = 0.1F;
    float farPlane = 100.0F;

    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projection(float aspectRatio) const;
};

} // namespace grimlock
