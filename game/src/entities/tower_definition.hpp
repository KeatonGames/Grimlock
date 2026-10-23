#pragma once

#include "entities/tower.hpp"

namespace grimlock::game {

struct TowerLevelDefinition {
    float damage = 25;
    double attackRange = 3.5;
    double fireRate = 1;           // Shots per second.
    std::uint64_t upgradeCost = 0; // Cost to enter this level; Level 1 uses constructionCost instead.
};

struct TowerDefinition {
    std::string id = "basic", name = "Small Tower";
    std::filesystem::path model = "models/cube.obj";
    glm::dvec3 halfExtent{0.45, 0.75, 0.45};
    glm::dvec3 visualScale{0.45, 0.75, 0.45};
    glm::vec4 tint{0.32F, 0.48F, 0.72F, 1};
    std::uint64_t constructionCost = 0;
    float maximumHealth = 250;
    std::vector<TowerLevelDefinition> levels{{}};
    double projectileSpeed = 9;
    double projectileRadius = 0.06;
    glm::vec4 projectileTint{1, 0.8F, 0.25F, 1};
    [[nodiscard]] Tower instance() const;
    [[nodiscard]] const TowerLevelDefinition& statistics(std::size_t level = 1) const;
};

[[nodiscard]] std::vector<TowerDefinition> loadTowerDefinitions(const std::filesystem::path& assetRoot);

} // namespace grimlock::game
