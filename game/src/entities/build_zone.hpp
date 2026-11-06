#pragma once

#include "entities/tower.hpp"

namespace grimlock::game {

struct BuildZone {
    ObjectId object = 0;
    std::string name;
    glm::dvec3 position{0};
    double yaw = 0;
    glm::dvec2 dimensions{4, 4};
    glm::dvec3 constructionOffset{0}; // Metres in the zone's rotated frame, independent of its dimensions.
    double constructionYaw = 0;
    ObjectId constructionObject = 0;
    std::vector<ecs::Entity> occupants; // Generation-safe handles; multiple entries only for legacy scenes.

    [[nodiscard]] glm::dvec3 constructionPosition() const;
    [[nodiscard]] bool covers(glm::dvec3 foot) const;
    [[nodiscard]] Tower snap(Tower tower) const;
    [[nodiscard]] bool contains(const Tower& tower) const;
    [[nodiscard]] std::array<glm::vec3, 4> corners() const;
    bool operator==(const BuildZone&) const = default;
};

} // namespace grimlock::game
