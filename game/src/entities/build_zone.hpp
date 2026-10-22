#pragma once

#include "entities/tower.hpp"

namespace grimlock::game {

struct BuildZone {
    ObjectId object = 0;
    std::string name;
    glm::dvec3 position{0};
    double yaw = 0;
    glm::dvec2 dimensions{4, 4};

    [[nodiscard]] bool contains(const Tower& tower) const;
    [[nodiscard]] std::array<glm::vec3, 4> corners() const;
    bool operator==(const BuildZone&) const = default;
};

} // namespace grimlock::game
