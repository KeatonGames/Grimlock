#pragma once

#include "entities/tower.hpp"

namespace grimlock::game {

struct TowerDefinition {
    std::string id = "basic", name = "Basic tower";
    std::filesystem::path model = "models/cube.obj";
    glm::dvec3 halfExtent{0.45, 0.75, 0.45};
    glm::dvec3 visualScale{0.45, 0.75, 0.45};
    glm::vec4 tint{0.32F, 0.48F, 0.72F, 1};
    [[nodiscard]] Tower instance() const;
};

[[nodiscard]] std::vector<TowerDefinition> loadTowerDefinitions(const std::filesystem::path& assetRoot);

} // namespace grimlock::game
