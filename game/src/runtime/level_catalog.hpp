#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace grimlock::game {

struct AvailableLevel {
    std::filesystem::path path;
    std::string name, description;
    std::string id;
    std::string requiredLevel;
};

struct LevelCatalog {
    std::vector<AvailableLevel> levels;
    std::vector<std::string> errors;
};

class PlayerProgress;
[[nodiscard]] bool levelUnlocked(const AvailableLevel& level, const PlayerProgress* progress) noexcept;

// CPU-only discovery using the same definitions and scenes as Editor and LevelRuntime.
[[nodiscard]] LevelCatalog discoverLevels(const std::filesystem::path& assetRoot);

} // namespace grimlock::game
