#pragma once

#include <grimlock/scene.hpp>
#include <grimlock/terrain.hpp>

namespace grimlock::game {

struct TowerProgress {
    std::string type;
    std::size_t level = 1;
    float health = 250, maximumHealth = 250;
    double hitFlashRemaining = 0;
};

// Scene IDs remain the selection/ownership handles. Footprints never depend on imported model bounds or scale.
struct Tower {
    ObjectId object = 0;
    std::string type = "basic";
    glm::dvec3 position{0}; // Center, not foot position.
    glm::dquat rotation{1, 0, 0, 0};
    std::filesystem::path model = "models/cube.obj";
    glm::dvec3 halfExtent{0.45, 0.75, 0.45};
    glm::dvec3 visualScale{0.45, 0.75, 0.45};

    [[nodiscard]] TerrainBox bounds() const;
    bool operator==(const Tower&) const = default;
};

} // namespace grimlock::game
