#include "entities/hero.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace grimlock::game {

namespace {
HeroDefinition readHeroDefinition(const std::filesystem::path& assets, const nlohmann::json& json) {
    HeroDefinition result;
    result.id = json.value("id", result.id);
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
    result.hitAnimation = json.value("hit_animation", result.hitAnimation);
    result.deathAnimation = json.value("death_animation", result.deathAnimation);
    const auto combat = json.value("combat", nlohmann::json::object());
    auto& stats = result.combat;
    const auto attack = combat.value("attack", std::string{"melee"});
    if (attack == "ranged") {
        stats.attack = HeroAttack::ranged;
    } else if (attack == "area") {
        stats.attack = HeroAttack::area;
    } else if (attack == "ranged_area") {
        stats.attack = HeroAttack::rangedArea;
    } else if (attack != "melee") {
        throw std::invalid_argument("Unknown hero attack type.");
    }
    stats.projectileSpeed = combat.value("projectile_speed", stats.projectileSpeed);
    stats.projectileRadius = combat.value("projectile_radius", stats.projectileRadius);
    stats.damage = combat.value("damage", stats.damage);
    const auto damageType = combat.value("damage_type", std::string{"physical"});
    if (damageType != "physical" && damageType != "magic") {
        throw std::invalid_argument("Unknown hero damage type: " + damageType);
    }
    stats.damageType = damageType == "magic" ? DamageType::magic : DamageType::physical;
    stats.range = combat.value("range", stats.range);
    stats.interval = combat.value("attack_interval", stats.interval);
    stats.impactTime = combat.value("impact_time", stats.impactTime);
    stats.acquisitionRange = combat.value("acquisition_range", stats.acquisitionRange);
    stats.pursuitRange = combat.value("pursuit_range", stats.pursuitRange);
    stats.attackAnimation = combat.value("attack_animation", stats.attackAnimation);
    stats.releaseAnimation = combat.value("release_animation", stats.releaseAnimation);
    stats.aimingAnimation = combat.value("aiming_animation", stats.aimingAnimation);
    stats.projectileSocket = combat.value("projectile_socket", stats.projectileSocket);
    stats.projectileModel = combat.value("projectile_model", std::string{});
    stats.projectileScale = combat.value("projectile_scale", stats.projectileScale);
    stats.splashRadius = combat.value("splash_radius", stats.splashRadius);
    stats.aimingTime = combat.value("aiming_time", stats.aimingTime);
    if (combat.contains("projectile_offset")) {
        const auto offset = combat.at("projectile_offset").get<std::array<float, 3>>();
        stats.projectileOffset = {offset[0], offset[1], offset[2]};
    }
    if (combat.contains("projectile_tint")) {
        const auto tint = combat.at("projectile_tint").get<std::array<float, 4>>();
        stats.projectileTint = {tint[0], tint[1], tint[2], tint[3]};
    }
    if (!std::isfinite(stats.projectileScale) || stats.projectileScale < 0 || !std::isfinite(stats.splashRadius) ||
        stats.splashRadius < 0 || stats.splashRadius > 100 || !std::isfinite(glm::length(stats.projectileOffset)) ||
        !std::isfinite(glm::length(stats.projectileTint)) || !std::isfinite(stats.aimingTime) || stats.aimingTime < 0 ||
        stats.aimingTime > stats.impactTime || (stats.attack == HeroAttack::rangedArea && stats.splashRadius <= 0)) {
        throw std::invalid_argument("Invalid hero projectile configuration.");
    }
    if (!stats.projectileModel.empty()) {
        bool safeProjectile = !stats.projectileModel.is_absolute() && !stats.projectileModel.has_root_name();
        for (const auto& part : stats.projectileModel) {
            safeProjectile = safeProjectile && part != "..";
        }
        if (!safeProjectile || !std::filesystem::is_regular_file(assets / stats.projectileModel)) {
            throw std::invalid_argument("Invalid hero projectile model.");
        }
    }
    stats.specialAbilities = combat.value("special_abilities", stats.specialAbilities);
    const auto ability = combat.value("cleave", nlohmann::json::object());
    stats.cleave.damage = ability.value("damage", stats.cleave.damage);
    stats.cleave.radius = ability.value("radius", stats.cleave.radius);
    stats.cleave.angle = ability.value("angle", stats.cleave.angle);
    stats.cleave.cooldown = ability.value("cooldown", stats.cleave.cooldown);
    for (const auto value : {static_cast<double>(stats.damage),
                             stats.range,
                             stats.projectileSpeed,
                             stats.projectileRadius,
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
    if (!safe || result.id.empty() || result.name.empty() || !std::isfinite(result.forwardYaw) ||
        result.radius > 1000 || result.height > 1000 || result.height < 0.15 || result.speed > 20 ||
        !std::filesystem::is_regular_file(assets / result.model)) {
        throw std::invalid_argument("Invalid hero model or configuration.");
    }
    return result;
}

} // namespace

HeroDefinition loadHeroDefinition(const std::filesystem::path& assets) {
    std::ifstream file{assets / "config/hero.json"};
    if (!file) {
        throw std::runtime_error("Cannot open config/hero.json.");
    }
    return readHeroDefinition(assets, nlohmann::json::parse(file));
}

std::vector<HeroDefinition> loadHeroDefinitions(const std::filesystem::path& assets) {
    std::vector<HeroDefinition> result{loadHeroDefinition(assets)};
    std::ifstream file{assets / "config/heroes.json"};
    if (!file) {
        throw std::runtime_error("Cannot open config/heroes.json.");
    }
    for (const auto& json : nlohmann::json::parse(file)) {
        auto definition = readHeroDefinition(assets, json);
        for (const auto& existing : result) {
            if (existing.id == definition.id) {
                throw std::invalid_argument("Duplicate hero type: " + definition.id);
            }
        }
        result.push_back(std::move(definition));
    }
    return result;
}

} // namespace grimlock::game
