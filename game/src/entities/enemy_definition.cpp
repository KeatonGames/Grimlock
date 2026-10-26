#include "entities/enemy_definition.hpp"

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace grimlock::game {
namespace {

void readCombat(const nlohmann::json& source, EnemyCombatDefinition& combat) {
    combat.damage = source.value("damage", combat.damage);
    combat.range = source.value("range", combat.range);
    combat.interval = source.value("attack_interval", combat.interval);
    combat.impactTime = source.value("impact_time", combat.impactTime);
    combat.aggressionRange = source.value("aggression_range", combat.aggressionRange);
    combat.pursuitRange = source.value("pursuit_range", combat.pursuitRange);
    combat.heroPriority = source.value("hero_priority", combat.heroPriority);
    combat.towerPriority = source.value("tower_priority", combat.towerPriority);
    combat.objectivePriority = source.value("objective_priority", combat.objectivePriority);
    combat.attackAnimation = source.value("attack_animation", combat.attackAnimation);
    combat.objectiveFirst = source.value("objective_first", combat.objectiveFirst);
    combat.obstructionRange = source.value("obstruction_range", combat.obstructionRange);
    combat.threatMemory = source.value("threat_memory", combat.threatMemory);
    combat.heroDamageModifier = source.value("hero_damage_modifier", combat.heroDamageModifier);
    combat.towerDamageModifier = source.value("tower_damage_modifier", combat.towerDamageModifier);
}

} // namespace

void validateEnemyDefinition(const EnemyDefinition& d) {
    const auto& c = d.combat;
    if (!std::isfinite(c.damage) || c.damage <= 0 || !std::isfinite(c.range) || c.range <= 0 ||
        !std::isfinite(c.interval) || c.interval < 1.0 / 60 || !std::isfinite(c.impactTime) || c.impactTime < 0 ||
        c.impactTime > c.interval || !std::isfinite(c.aggressionRange) || c.aggressionRange <= 0 ||
        !std::isfinite(c.pursuitRange) || c.pursuitRange < c.aggressionRange || c.heroPriority < 0 ||
        c.towerPriority < 0 || c.objectivePriority < 0 || !std::isfinite(c.obstructionRange) ||
        c.obstructionRange < 0 || !std::isfinite(c.threatMemory) || c.threatMemory < 0 ||
        !std::isfinite(c.heroDamageModifier) || c.heroDamageModifier < 0 || !std::isfinite(c.towerDamageModifier) ||
        c.towerDamageModifier < 0 || !std::isfinite(c.damage * c.heroDamageModifier) ||
        !std::isfinite(c.damage * c.towerDamageModifier)) {
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
        d.showHealthBar = entry.value("show_health_bar", d.showHealthBar);
        d.acceleration = entry.value("acceleration", d.acceleration);
        d.deceleration = entry.value("deceleration", d.deceleration);
        d.stoppingDistance = entry.value("stopping_distance", d.stoppingDistance);
        d.retryInterval = entry.value("retry_interval", d.retryInterval);
        d.stuckTimeout = entry.value("stuck_timeout", d.stuckTimeout);
        d.movementAnimation = entry.value("movement_animation", d.movementAnimation);
        d.animationSpeed = entry.value("animation_speed", d.animationSpeed);
        d.forwardYaw = entry.value("forward_yaw", d.forwardYaw);
        d.despawnOnArrival = entry.value("despawn_on_arrival", d.despawnOnArrival);
        readCombat(entry.value("combat", nlohmann::json::object()), d.combat);
        validateEnemyDefinition(d);
        if (std::filesystem::is_regular_file(assetRoot / d.model)) {
            definitions.push_back(std::move(d));
        }
    }
    if (definitions.empty() || definitions.front().model != "models/chicken.glb") {
        throw std::invalid_argument("Animal definitions must list the available chicken model first.");
    }
    // Archetypes inherit physical dimensions and presentation from a named animal, not AI behavior from its model.
    const auto archetypePath = assetRoot / "config/enemy_archetypes.json";
    if (std::filesystem::exists(archetypePath)) {
        std::ifstream archetypes{archetypePath};
        if (!archetypes) {
            throw std::runtime_error("Cannot open enemy archetypes: " + archetypePath.string());
        }
        const auto entries = nlohmann::json::parse(archetypes);
        if (!entries.is_array()) {
            throw std::invalid_argument("Enemy archetypes must be an array.");
        }
        const auto animalCount = definitions.size();
        std::set<std::string> names;
        for (const auto& definition : definitions) {
            names.insert(definition.name);
        }
        for (const auto& entry : entries) {
            const auto animal = entry.at("animal").get<std::string>();
            std::size_t index = 0;
            while (index < animalCount && definitions[index].name != animal) {
                ++index;
            }
            if (index == animalCount) {
                throw std::invalid_argument("Unknown archetype animal: " + animal);
            }
            auto d = definitions[index];
            d.name = entry.at("name").get<std::string>();
            if (!names.insert(d.name).second) {
                throw std::invalid_argument("Duplicate enemy definition: " + d.name);
            }
            d.maximumHealth = entry.at("maximum_health").get<float>();
            d.speed = entry.at("speed").get<double>();
            d.radius = entry.value("radius", d.radius);
            d.showHealthBar = true;
            const auto attackAnimation = d.combat.attackAnimation;
            d.combat = {};
            d.combat.attackAnimation = attackAnimation;
            readCombat(entry.at("combat"), d.combat);
            validateEnemyDefinition(d);
            definitions.push_back(std::move(d));
        }
    }
    return definitions;
}

} // namespace grimlock::game
