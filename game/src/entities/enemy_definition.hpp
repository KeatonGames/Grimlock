#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace grimlock::game {

struct EnemyCombatDefinition {
    float damage = 8;
    double range = 0.35, interval = 1, impactTime = 0.25;
    double aggressionRange = 4, pursuitRange = 6;
    int heroPriority = 3, towerPriority = 2, objectivePriority = 1;
    std::string attackAnimation = "attack";
    bool objectiveFirst = false;
    double obstructionRange = 0.8, threatMemory = 2;
    float heroDamageModifier = 1, towerDamageModifier = 1;
};

using EnemyTypeId = std::size_t;

struct EnemyDefinition {
    std::string name = "Chicken";
    std::filesystem::path model = "models/chicken.glb";
    double speed = 1.5;
    double radius = 0.195;
    double height = 0.45;
    double acceleration = 8;
    double deceleration = 12;
    double stoppingDistance = 0; // Zero uses the physical radius.
    double retryInterval = 1;
    double stuckTimeout = 2;
    float visualHeight = 0.36F; // Independent of the physical radius and headroom.
    float maximumHealth = 100;
    std::string movementAnimation = "walk";
    float animationSpeed = 1;
    double forwardYaw = 0;
    bool despawnOnArrival = true;
    EnemyCombatDefinition combat;
    bool showHealthBar = false;
};

void validateEnemyDefinition(const EnemyDefinition& definition);
[[nodiscard]] std::vector<EnemyDefinition> loadAnimalDefinitions(const std::filesystem::path& assetRoot);

} // namespace grimlock::game
