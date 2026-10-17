#include "grimlock/transform.hpp"
#include <glm/ext/matrix_transform.hpp>

namespace grimlock {
glm::mat4 Transform::matrix() const {
    return glm::translate(glm::mat4{1},position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4{1},scale);
}
}
