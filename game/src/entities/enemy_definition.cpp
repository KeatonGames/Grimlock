#include "entities/enemy_definition.hpp"

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace grimlock::game {

void validateEnemyDefinition(const EnemyDefinition& d) {
    const auto& c = d.combat;
    if (!std::isfinite(c.damage) || c.damage <= 0 || !std::isfinite(c.range) || c.range <= 0 ||
        !std::isfinite(c.interval) || c.interval < 1.0 / 60 || !std::isfinite(c.impactTime) || c.impactTime < 0 ||
        c.impactTime > c.interval || !std::isfinite(c.aggressionRange) || c.aggressionRange <= 0 ||
        !std::isfinite(c.pursuitRange) || c.pursuitRange < c.aggressionRange || c.heroPriority < 0 ||
        c.towerPriority < 0 || c.objectivePriority < 0) {
        throw std::invalid_argument("Invalid enemy combat statistics or target priorities.");
    }
    if (d.name.empty() || d.model.empty() || !std::isfinite(d.speed) || d.speed < 0 || d.speed > 20 ||
        !std::isfinite(d.radius) || d.radius <= 0 || d.radius > 1000 || !std::isfinite(d.height) || d.height < 0.15 ||
        d.height > 1000 || !std::isfinite(d.acceleration) || d.acceleration <= 0 || !std::isfinite(d.deceleration) ||
        d.deceleration <= 0 || !std::isfinite(d.visualHeight) || d.visualHeight <= 0 ||
        !std::isfinite(d.maximumHealth) || d.maximumHealth <= 0 || !std::isfinite(d.animationSpeed) ||
        d.animationSpeed < 0 || !std::isfinite(d.forwardYaw) || !std::isfinite(d.stoppingDistance) ||
        d.stoppingDistance < 0 || !std::isfinite(d.retryInterval) || d.retryInterval <= 0 ||
        !std::isfinite(d.stuckTimeout) || d.stuckTimeout <= 0) {
        throw std::invalid_argument("Invalid enemy model, movement, physical dimensions or animation configuration.");
    }
}

std::vector<EnemyDefinition> loadAnimalDefinitions(const std::filesystem::path& assetRoot) {
    const auto path = assetRoot / "config/animals.json";
    std::ifstream stream{path};
    if (!stream) {
        throw std::runtime_error("Cannot open animal definitions: " + path.string());
    }
    const auto source = nlohmann::json::parse(stream);
    if (!source.is_array()) {
        throw std::invalid_argument("Animal definitions must be an array.");
    }
    std::vector<EnemyDefinition> definitions;
    std::set<std::filesystem::path> models;
    for (const auto& entry : source) {
        EnemyDefinition d;
        d.name = entry.at("name").get<std::string>();
        d.model = std::filesystem::path{entry.at("model").get<std::string>()}.lexically_normal();
        if (d.model.is_absolute() || d.model.parent_path() != "models" || d.model.extension() != ".glb" ||
            !models.insert(d.model).second) {
            throw std::invalid_argument("Animal models must be unique GLB files in assets/models.");
        }
        d.radius = entry.at("radius").get<double>();
        d.height = entry.at("height").get<double>();
        d.visualHeight = entry.at("visual_height").get<float>();
        d.speed = entry.value("speed", d.speed);
        d.maximumHealth = entry.value("maximum_health", d.maximumHealth);
        d.acceleration = entry.value("acceleration", d.acceleration);
        d.deceleration = entry.value("deceleration", d.deceleration);
        d.stoppingDistance = entry.value("stopping_distance", d.stoppingDistance);
        d.retryInterval = entry.value("retry_interval", d.retryInterval);
        d.stuckTimeout = entry.value("stuck_timeout", d.stuckTimeout);
        d.movementAnimation = entry.value("movement_animation", d.movementAnimation);
        d.animationSpeed = entry.value("animation_speed", d.animationSpeed);
        d.forwardYaw = entry.value("forward_yaw", d.forwardYaw);
        d.despawnOnArrival = entry.value("despawn_on_arrival", d.despawnOnArrival);
        const auto combat = entry.value("combat", nlohmann::json::object());
        auto& c = d.combat;
        c.damage = combat.value("damage", c.damage);
        c.range = combat.value("range", c.range);
        c.interval = combat.value("attack_interval", c.interval);
        c.impactTime = combat.value("impact_time", c.impactTime);
        c.aggressionRange = combat.value("aggression_range", c.aggressionRange);
        c.pursuitRange = combat.value("pursuit_range", c.pursuitRange);
        c.heroPriority = combat.value("hero_priority", c.heroPriority);
        c.towerPriority = combat.value("tower_priority", c.towerPriority);
        c.objectivePriority = combat.value("objective_priority", c.objectivePriority);
        c.attackAnimation = combat.value("attack_animation", c.attackAnimation);
        validateEnemyDefinition(d);
        if (std::filesystem::is_regular_file(assetRoot / d.model)) {
            definitions.push_back(std::move(d));
        }
    }
    if (definitions.empty() || definitions.front().model != "models/chicken.glb") {
        throw std::invalid_argument("Animal definitions must list the available chicken model first.");
    }
    return definitions;
}

} // namespace grimlock::game
