#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace grimlock::game {

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
};

void validateEnemyDefinition(const EnemyDefinition& definition);
[[nodiscard]] std::vector<EnemyDefinition> loadAnimalDefinitions(const std::filesystem::path& assetRoot);

} // namespace grimlock::game
