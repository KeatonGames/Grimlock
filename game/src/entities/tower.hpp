#pragma once

#include "entities/components.hpp"

#include <grimlock/ecs.hpp>
#include <grimlock/scene.hpp>
#include <grimlock/terrain.hpp>

namespace grimlock::game {

struct TowerProgress : HealthComponent {
    TowerProgress(std::string configuredType = "",
                  std::size_t configuredLevel = 1,
                  float current = 250,
                  float maximum = 250)
        : HealthComponent{current, maximum, 0}, type(std::move(configuredType)), level(configuredLevel) {
    }

    std::string type;
    std::size_t level = 1;
};

// Scene IDs remain the selection/ownership handles. Footprints never depend on imported model bounds or scale.
struct Tower {
    ObjectId object = 0;
    ecs::Entity entity = 0; // Runtime identity; object is the Scene/Editor bridge only.
    ObjectId buildZone = 0;
    std::string type = "basic";
    glm::dvec3 position{0}; // Center, not foot position.
    glm::dquat rotation{1, 0, 0, 0};
    std::filesystem::path model = "towers/structures/primitives/cube.glb";
    glm::dvec3 halfExtent{0.45, 0.75, 0.45};
    glm::dvec3 visualScale{0.45, 0.75, 0.45};
    double firingHeight = 0; // Height above the foundation; zero retains the legacy collider-top origin.
    glm::dvec2 firingOffset{0};

    [[nodiscard]] TerrainBox bounds() const;
    bool operator==(const Tower&) const = default;
};

} // namespace grimlock::game
