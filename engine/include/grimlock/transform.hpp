#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace grimlock {
struct Transform {
    glm::vec3 position{0};
    glm::quat rotation{1, 0, 0, 0};
    glm::vec3 scale{1};
    [[nodiscard]] glm::mat4 matrix() const;
};
} // namespace grimlock
