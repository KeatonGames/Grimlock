#pragma once

#include "entities/enemy.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace grimlock::game {

struct LevelSpawn {
    std::string name;
    SpawnArea area;
};

struct WaveGroup {
    std::string enemy;
    std::size_t spawn = 0;
    std::size_t count = 1;
    std::size_t batchSize = 1;
    double start = 0;
    double interval = 1;
};

struct WaveDefinition {
    std::string name;
    std::vector<WaveGroup> groups;
    std::uint64_t goldReward = 0;
};

struct LevelDefinition {
    std::string name;
    std::filesystem::path scene;
    glm::dvec2 objectivePosition{0};
    std::uint64_t objectiveHealth = 1;
    std::uint64_t damagePerArrival = 1;
    std::vector<LevelSpawn> spawns;
    std::vector<WaveDefinition> waves;
    std::uint64_t startingGold = 0;
};

// Scene paths are relative to the asset root; spawn/objective coordinates are world X/Z.
[[nodiscard]] LevelDefinition loadLevelDefinition(const std::filesystem::path& path);
void validateLevelDefinition(const LevelDefinition& definition);
void saveLevelDefinition(const std::filesystem::path& path, const LevelDefinition& definition);

} // namespace grimlock::game
