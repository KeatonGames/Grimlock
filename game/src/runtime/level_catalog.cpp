#include "runtime/level_catalog.hpp"

#include "entities/enemy_definition.hpp"
#include "entities/level_definition.hpp"

#include <algorithm>
#include <grimlock/scene.hpp>
#include <set>
#include <stdexcept>

namespace grimlock::game {

LevelCatalog discoverLevels(const std::filesystem::path& assetRoot) {
    LevelCatalog result;
    std::set<std::string> enemies;
    try {
        for (const auto& enemy : loadAnimalDefinitions(assetRoot)) {
            if (enemy.despawnOnArrival) {
                enemies.insert(enemy.name);
            }
        }
    } catch (const std::exception& error) {
        result.errors.push_back(error.what());
        return result;
    }
    std::error_code error;
    std::filesystem::recursive_directory_iterator files{assetRoot / "levels", error}, end;
    if (error) {
        result.errors.push_back("Cannot read level directory: " + error.message());
        return result;
    }
    while (files != end) {
        const auto path = files->path();
        if (files->is_regular_file(error) && path.extension() == ".json") {
            try {
                auto definition = loadLevelDefinition(path);
                const auto scene = Scene::load(assetRoot / definition.scene);
                for (const auto& [id, object] : scene->objects()) {
                    if (object.model && !std::filesystem::is_regular_file(assetRoot / *object.model)) {
                        throw std::invalid_argument("Missing scene model: " + object.model->generic_string());
                    }
                }
                for (const auto& wave : definition.waves) {
                    for (const auto& group : wave.groups) {
                        if (!enemies.contains(group.enemy)) {
                            throw std::invalid_argument("Unknown enemy definition: " + group.enemy);
                        }
                    }
                }
                result.levels.push_back({std::filesystem::absolute(path),
                                         std::move(definition.name),
                                         std::move(definition.description),
                                         std::move(definition.id)});
            } catch (const std::exception& failure) {
                result.errors.push_back(path.filename().string() + ": " + failure.what());
            }
        }
        files.increment(error);
        if (error) {
            result.errors.push_back("Cannot enumerate levels: " + error.message());
            break;
        }
    }
    std::set<std::string> ids, duplicates;
    for (const auto& level : result.levels) {
        if (!level.id.empty() && !ids.insert(level.id).second) {
            duplicates.insert(level.id);
        }
    }
    std::erase_if(result.levels, [&](const AvailableLevel& level) {
        if (!duplicates.contains(level.id)) {
            return false;
        }
        result.errors.push_back(level.path.filename().string() +
                                ": duplicate level ID; assign a new ID to independent copies.");
        return true;
    });
    std::sort(result.levels.begin(), result.levels.end(), [](const AvailableLevel& a, const AvailableLevel& b) {
        return a.name != b.name ? a.name < b.name : a.path < b.path;
    });
    return result;
}

} // namespace grimlock::game
