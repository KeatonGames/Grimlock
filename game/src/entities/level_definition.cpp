#include "entities/level_definition.hpp"

#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace grimlock::game {
namespace {

glm::dvec2 point(const nlohmann::json& value) {
    if (!value.is_array() || value.size() != 2) {
        throw std::invalid_argument("Level coordinates require exactly two world X/Z values.");
    }
    return {value.at(0).get<double>(), value.at(1).get<double>()};
}

std::uint64_t positiveInteger(const nlohmann::json& value) {
    if (!value.is_number_integer() ||
        (value.is_number_unsigned() ? value.get<std::uint64_t>() == 0 : value.get<std::int64_t>() <= 0)) {
        throw std::invalid_argument("Level counts and health must be positive integers.");
    }
    return value.get<std::uint64_t>();
}

bool finite(glm::dvec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

std::uint64_t gold(const nlohmann::json& value) {
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0)) {
        throw std::invalid_argument("Gold amounts must be nonnegative integers.");
    }
    return value.get<std::uint64_t>();
}

} // namespace

void validateLevelDefinition(const LevelDefinition& definition) {
    if (definition.name.empty() || definition.scene.empty() || definition.scene.is_absolute() ||
        definition.scene.has_root_name() || definition.scene.extension() != ".json" ||
        !finite(definition.objectivePosition) || !definition.objectiveHealth || !definition.damagePerArrival ||
        definition.spawns.empty() || definition.waves.empty()) {
        throw std::invalid_argument("Level requires a name, relative scene, objective, spawns and waves.");
    }
    for (const auto& part : definition.scene) {
        if (part == "..") {
            throw std::invalid_argument("Level scene must stay inside the asset directory.");
        }
    }
    std::set<std::string> names;
    for (const auto& spawn : definition.spawns) {
        const auto& area = spawn.area;
        if (spawn.name.empty() || !names.insert(spawn.name).second || !finite(area.minimum) || !finite(area.maximum) ||
            !std::isfinite(area.height) || area.minimum.x > area.maximum.x || area.minimum.y > area.maximum.y) {
            throw std::invalid_argument("Level spawn names must be unique and rectangles finite and ordered.");
        }
    }
    auto maximumGold = definition.startingGold;
    for (const auto& wave : definition.waves) {
        if (wave.goldReward > std::numeric_limits<std::uint64_t>::max() - maximumGold) {
            throw std::invalid_argument("Starting gold and wave rewards exceed the supported gold balance.");
        }
        maximumGold += wave.goldReward;
        if (wave.name.empty() || wave.groups.empty()) {
            throw std::invalid_argument("Every wave requires a name and explicit groups.");
        }
        std::size_t total = 0;
        for (const auto& group : wave.groups) {
            if (group.enemy.empty() || group.spawn >= definition.spawns.size() || !group.count || !group.batchSize ||
                group.batchSize > group.count || group.count > 1000 - total || !std::isfinite(group.start) ||
                group.start < 0 || !std::isfinite(group.interval) || group.interval < 0 ||
                !std::isfinite(group.start + group.interval * static_cast<double>(group.count))) {
                throw std::invalid_argument("Invalid wave group: check type, spawn, counts (1000/wave) and timing.");
            }
            total += group.count;
        }
    }
}

LevelDefinition loadLevelDefinition(const std::filesystem::path& path) {
    std::ifstream file{path};
    if (!file) {
        throw std::runtime_error("Cannot open level: " + path.string());
    }
    const auto document = nlohmann::json::parse(file);
    if (document.at("version").get<int>() != 1) {
        throw std::invalid_argument("Unsupported level version.");
    }
    LevelDefinition result;
    result.name = document.at("name").get<std::string>();
    result.scene = document.at("scene").get<std::string>();
    result.startingGold = document.contains("starting_gold") ? gold(document.at("starting_gold")) : 0;
    const auto& objective = document.at("objective");
    result.objectivePosition = point(objective.at("position"));
    result.objectiveHealth = positiveInteger(objective.at("health"));
    result.damagePerArrival = positiveInteger(objective.at("damage_per_arrival"));
    for (const auto& entry : document.at("spawns")) {
        result.spawns.push_back(
            {entry.at("name").get<std::string>(),
             {point(entry.at("minimum")), point(entry.at("maximum")), entry.at("height").get<double>()}});
    }
    for (const auto& entry : document.at("waves")) {
        WaveDefinition wave{entry.at("name").get<std::string>(), {}};
        wave.goldReward = entry.contains("gold_reward") ? gold(entry.at("gold_reward")) : 0;
        for (const auto& value : entry.at("groups")) {
            const auto spawnName = value.at("spawn").get<std::string>();
            std::size_t spawn = 0;
            while (spawn < result.spawns.size() && result.spawns[spawn].name != spawnName) {
                ++spawn;
            }
            wave.groups.push_back({value.at("enemy").get<std::string>(),
                                   spawn,
                                   static_cast<std::size_t>(positiveInteger(value.at("count"))),
                                   static_cast<std::size_t>(positiveInteger(value.at("batch_size"))),
                                   value.at("start").get<double>(),
                                   value.at("interval").get<double>()});
        }
        result.waves.push_back(std::move(wave));
    }
    validateLevelDefinition(result);
    return result;
}

void saveLevelDefinition(const std::filesystem::path& path, const LevelDefinition& definition) {
    validateLevelDefinition(definition);
    nlohmann::json document{{"version", 1},
                            {"name", definition.name},
                            {"scene", definition.scene.generic_string()},
                            {"starting_gold", definition.startingGold},
                            {"objective",
                             {{"position", {definition.objectivePosition.x, definition.objectivePosition.y}},
                              {"health", definition.objectiveHealth},
                              {"damage_per_arrival", definition.damagePerArrival}}},
                            {"spawns", nlohmann::json::array()},
                            {"waves", nlohmann::json::array()}};
    for (const auto& spawn : definition.spawns) {
        document["spawns"].push_back({{"name", spawn.name},
                                      {"minimum", {spawn.area.minimum.x, spawn.area.minimum.y}},
                                      {"maximum", {spawn.area.maximum.x, spawn.area.maximum.y}},
                                      {"height", spawn.area.height}});
    }
    for (const auto& wave : definition.waves) {
        nlohmann::json entry{{"name", wave.name},
                             {"gold_reward", wave.goldReward},
                             {"groups", nlohmann::json::array()}};
        for (const auto& group : wave.groups) {
            entry["groups"].push_back({{"enemy", group.enemy},
                                       {"spawn", definition.spawns[group.spawn].name},
                                       {"count", group.count},
                                       {"batch_size", group.batchSize},
                                       {"start", group.start},
                                       {"interval", group.interval}});
        }
        document["waves"].push_back(std::move(entry));
    }
    std::ofstream file{path};
    file << document.dump(2) << '\n';
    file.close();
    if (!file) {
        throw std::runtime_error("Cannot save level definition: " + path.string());
    }
}

} // namespace grimlock::game
