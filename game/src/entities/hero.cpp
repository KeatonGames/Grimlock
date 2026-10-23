#include "entities/hero.hpp"

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace grimlock::game {

HeroDefinition loadHeroDefinition(const std::filesystem::path& assets) {
    std::ifstream file{assets / "config/hero.json"};
    if (!file) {
        throw std::runtime_error("Cannot open config/hero.json.");
    }
    const auto json = nlohmann::json::parse(file);
    HeroDefinition result;
    result.name = json.at("name").get<std::string>();
    result.model = json.at("model").get<std::string>();
    result.speed = json.value("speed", result.speed);
    result.radius = json.value("radius", result.radius);
    result.height = json.value("height", result.height);
    result.visualHeight = json.value("visual_height", result.visualHeight);
    result.maximumHealth = json.value("maximum_health", result.maximumHealth);
    result.acceleration = json.value("acceleration", result.acceleration);
    result.deceleration = json.value("deceleration", result.deceleration);
    result.turningSpeed = json.value("turning_speed", result.turningSpeed);
    result.stoppingDistance = json.value("stopping_distance", result.stoppingDistance);
    result.forwardYaw = json.value("forward_yaw", result.forwardYaw);
    result.idleAnimation = json.value("idle_animation", result.idleAnimation);
    result.walkAnimation = json.value("walk_animation", result.walkAnimation);
    result.runAnimation = json.value("run_animation", result.runAnimation);
    const auto combat = json.value("combat", nlohmann::json::object());
    auto& stats = result.combat;
    stats.damage = combat.value("damage", stats.damage);
    stats.range = combat.value("range", stats.range);
    stats.interval = combat.value("attack_interval", stats.interval);
    stats.impactTime = combat.value("impact_time", stats.impactTime);
    stats.acquisitionRange = combat.value("acquisition_range", stats.acquisitionRange);
    stats.pursuitRange = combat.value("pursuit_range", stats.pursuitRange);
    stats.attackAnimation = combat.value("attack_animation", stats.attackAnimation);
    const auto ability = combat.value("cleave", nlohmann::json::object());
    stats.cleave.damage = ability.value("damage", stats.cleave.damage);
    stats.cleave.radius = ability.value("radius", stats.cleave.radius);
    stats.cleave.angle = ability.value("angle", stats.cleave.angle);
    stats.cleave.cooldown = ability.value("cooldown", stats.cleave.cooldown);
    for (const auto value : {static_cast<double>(stats.damage),
                             stats.range,
                             stats.interval,
                             stats.acquisitionRange,
                             stats.pursuitRange,
                             static_cast<double>(stats.cleave.damage),
                             stats.cleave.radius,
                             stats.cleave.angle,
                             stats.cleave.cooldown}) {
        if (!std::isfinite(value) || value <= 0) {
            throw std::invalid_argument("Hero combat statistics must be positive and finite.");
        }
    }
    if (!std::isfinite(stats.impactTime) || stats.impactTime < 0 || stats.impactTime > stats.interval ||
        stats.cleave.angle > 360 || stats.pursuitRange < stats.acquisitionRange) {
        throw std::invalid_argument("Invalid hero attack timing, pursuit range or Cleave angle.");
    }
    bool safe = !result.model.is_absolute() && !result.model.has_root_name();
    for (const auto& part : result.model) {
        safe = safe && part != "..";
    }
    for (const auto value : {result.speed,
                             result.radius,
                             result.height,
                             static_cast<double>(result.visualHeight),
                             static_cast<double>(result.maximumHealth),
                             result.acceleration,
                             result.deceleration,
                             result.turningSpeed,
                             result.stoppingDistance}) {
        if (!std::isfinite(value) || value <= 0) {
            throw std::invalid_argument("Hero dimensions and movement values must be positive and finite.");
        }
    }
    if (!safe || result.name.empty() || !std::isfinite(result.forwardYaw) || result.radius > 1000 ||
        result.height > 1000 || result.height < 0.15 || result.speed > 20 ||
        !std::filesystem::is_regular_file(assets / result.model)) {
        throw std::invalid_argument("Invalid hero model or configuration.");
    }
    return result;
}

} // namespace grimlock::game
