#include "entities/tower_definition.hpp"

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace grimlock::game {

Tower TowerDefinition::instance() const {
    Tower tower;
    tower.type = id;
    tower.model = model;
    tower.halfExtent = halfExtent;
    tower.visualScale = visualScale;
    return tower;
}

std::vector<TowerDefinition> loadTowerDefinitions(const std::filesystem::path& assetRoot) {
    std::ifstream file{assetRoot / "config/towers.json"};
    if (!file) {
        throw std::runtime_error("Cannot open config/towers.json.");
    }
    const auto document = nlohmann::json::parse(file);
    if (!document.is_array() || document.empty()) {
        throw std::invalid_argument("Tower catalog must be a nonempty array.");
    }
    std::vector<TowerDefinition> result;
    std::set<std::string> ids;
    for (const auto& entry : document) {
        TowerDefinition definition;
        definition.id = entry.at("id").get<std::string>();
        definition.name = entry.at("name").get<std::string>();
        definition.model = entry.at("model").get<std::string>();
        definition.damage = entry.value("damage", definition.damage);
        definition.attackRange = entry.value("attack_range", definition.attackRange);
        definition.fireRate = entry.value("fire_rate", definition.fireRate);
        definition.projectileSpeed = entry.value("projectile_speed", definition.projectileSpeed);
        definition.projectileRadius = entry.value("projectile_radius", definition.projectileRadius);
        if (entry.contains("projectile_tint")) {
            const auto color = entry.at("projectile_tint").get<std::array<float, 4>>();
            definition.projectileTint = {color[0], color[1], color[2], color[3]};
        }
        if (!std::isfinite(definition.damage) || definition.damage <= 0 || !std::isfinite(definition.attackRange) ||
            definition.attackRange <= 0 || !std::isfinite(definition.fireRate) || definition.fireRate <= 0 ||
            definition.fireRate > 60 || !std::isfinite(definition.projectileSpeed) || definition.projectileSpeed <= 0 ||
            !std::isfinite(definition.projectileRadius) || definition.projectileRadius <= 0 ||
            !std::isfinite(glm::length(definition.projectileTint)) ||
            glm::any(glm::lessThan(definition.projectileTint, glm::vec4{0})) ||
            glm::any(glm::greaterThan(definition.projectileTint, glm::vec4{1}))) {
            throw std::invalid_argument("Tower combat statistics must be finite and positive (fire_rate <= 60).");
        }
        if (entry.contains("construction_cost")) {
            const auto& cost = entry.at("construction_cost");
            if (!cost.is_number_integer() || (!cost.is_number_unsigned() && cost.get<std::int64_t>() < 0)) {
                throw std::invalid_argument("Tower construction cost must be a nonnegative integer.");
            }
            definition.constructionCost = cost.get<std::uint64_t>();
        }
        const auto extent = entry.at("half_extent").get<std::array<double, 3>>();
        const auto scale = entry.at("visual_scale").get<std::array<double, 3>>();
        const auto tint = entry.at("tint").get<std::array<float, 4>>();
        definition.halfExtent = {extent[0], extent[1], extent[2]};
        definition.visualScale = {scale[0], scale[1], scale[2]};
        definition.tint = {tint[0], tint[1], tint[2], tint[3]};
        bool safePath =
            !definition.model.empty() && !definition.model.is_absolute() && !definition.model.has_root_name();
        for (const auto& part : definition.model) {
            safePath = safePath && part != "..";
        }
        if (definition.id.empty() || definition.id.find(':') != std::string::npos || definition.name.empty() ||
            !ids.insert(definition.id).second || !safePath ||
            !std::filesystem::is_regular_file(assetRoot / definition.model) ||
            !std::isfinite(glm::length(definition.halfExtent)) ||
            glm::any(glm::lessThanEqual(definition.halfExtent, glm::dvec3{0})) ||
            !std::isfinite(glm::length(definition.visualScale)) ||
            glm::any(glm::lessThanEqual(definition.visualScale, glm::dvec3{0})) ||
            !std::isfinite(glm::length(definition.tint)) || glm::any(glm::lessThan(definition.tint, glm::vec4{0})) ||
            glm::any(glm::greaterThan(definition.tint, glm::vec4{1}))) {
            throw std::invalid_argument("Invalid tower type, asset, footprint, scale or tint.");
        }
        result.push_back(std::move(definition));
    }
    if (!ids.contains("basic")) {
        throw std::invalid_argument("Tower catalog must retain the legacy basic type.");
    }
    return result;
}

} // namespace grimlock::game
