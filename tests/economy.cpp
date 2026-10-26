#include "entities/level_definition.hpp"
#include "entities/tower_definition.hpp"
#include "systems/economy_system.hpp"

#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

using namespace grimlock::game;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <class F> void rejects(F operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "invalid economy configuration accepted");
}

void transactions() {
    EconomySystem economy;
    require(economy.gold() == 0 && economy.trySpend(0) && !economy.trySpend(1), "zero balance transactions incorrect");
    economy.reset(100);
    require(economy.canAfford(30) && economy.trySpend(30) && economy.gold() == 70, "construction spending incorrect");
    require(!economy.trySpend(71) && economy.gold() == 70, "insufficient transaction changed gold");
    economy.earn(25);
    require(economy.gold() == 95, "fixed earning incorrect");
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    rejects([&] {
        economy.earn(maximum);
    });
    require(economy.gold() == 95, "overflow changed balance");
    economy.earn(maximum - 95);
    require(economy.trySpend(maximum) && economy.gold() == 0, "integer boundary transaction incorrect");
    economy.reset(12);
    require(economy.gold() == 12, "reset retained balance");
}

nlohmann::json read(const std::filesystem::path& path) {
    std::ifstream file{path};
    return nlohmann::json::parse(file);
}

void save(const std::filesystem::path& path, const nlohmann::json& document) {
    std::ofstream file{path};
    file << document.dump();
}

void configuration(const std::filesystem::path& assets, const std::filesystem::path& fixture) {
    const auto level = loadLevelDefinition(assets / "levels/first_watch.json");
    const auto towers = loadTowerDefinitions(assets);
    require(towers[0].levels.size() == 3 && towers[1].levels.size() == 3, "expected three authored tower levels");
    for (std::size_t level = 1; level <= 3; ++level) {
        const auto& small = towers[0].statistics(level);
        const auto& large = towers[1].statistics(level);
        require(large.damage > small.damage && large.attackRange > small.attackRange &&
                    large.damage * large.fireRate > small.damage * small.fireRate,
                "Large Tower must remain stronger at equivalent levels");
    }
    require(level.startingGold == 90 && level.objectiveHealth == 10 && level.waves.size() == 5 &&
                level.waves[0].goldReward == 25 && level.waves[1].goldReward == 35 && level.waves[2].goldReward == 45 &&
                level.waves[3].goldReward == 60 && level.waves[4].goldReward == 0 && towers[0].constructionCost == 30 &&
                towers[1].constructionCost == 60,
            "handcrafted economy values incorrect");
    std::filesystem::create_directories(fixture / "models");
    std::filesystem::create_directories(fixture / "config");
    std::ofstream{fixture / "models/cube.obj"};
    const auto levelPath = fixture / "level.json";
    const auto towerPath = fixture / "config/towers.json";
    auto levelJson = read(assets / "levels/first_watch.json");
    auto towerJson = read(assets / "config/towers.json");
    levelJson.erase("starting_gold");
    for (auto& wave : levelJson["waves"]) {
        wave.erase("gold_reward");
    }
    for (auto& tower : towerJson) {
        tower.erase("construction_cost");
    }
    save(levelPath, levelJson);
    save(towerPath, towerJson);
    for (const auto& invalid : nlohmann::json::array({-1, 1.5, true, "10", nullptr})) {
        auto modified = towerJson;
        modified[0]["levels"][1]["upgrade_cost"] = invalid;
        save(towerPath, modified);
        rejects([&] {
            (void)loadTowerDefinitions(fixture);
        });
    }
    for (const auto& invalid : nlohmann::json::array({nlohmann::json::array(), nullptr, 3})) {
        auto modified = towerJson;
        modified[0]["levels"] = invalid;
        save(towerPath, modified);
        rejects([&] {
            (void)loadTowerDefinitions(fixture);
        });
    }
    for (const auto* property : {"damage", "attack_range", "fire_rate", "upgrade_cost"}) {
        auto modified = towerJson;
        modified[0]["levels"][1].erase(property);
        save(towerPath, modified);
        rejects([&] {
            (void)loadTowerDefinitions(fixture);
        });
    }
    save(towerPath, towerJson);
    for (const auto* property : {"damage", "attack_range", "fire_rate", "projectile_speed", "projectile_radius"}) {
        for (const auto& invalid : nlohmann::json::array({-1, 0, "bad", nullptr})) {
            auto modified = towerJson;
            if (std::string_view{property} == "damage" || std::string_view{property} == "attack_range" ||
                std::string_view{property} == "fire_rate") {
                modified[0]["levels"][0][property] = invalid;
            } else {
                modified[0][property] = invalid;
            }
            save(towerPath, modified);
            rejects([&] {
                (void)loadTowerDefinitions(fixture);
            });
        }
    }
    auto legacy = towerJson;
    for (auto& tower : legacy) {
        tower.erase("levels");
        for (const auto* property :
             {"damage", "attack_range", "fire_rate", "projectile_speed", "projectile_radius", "projectile_tint"}) {
            tower.erase(property);
        }
    }
    save(towerPath, legacy);
    require(loadTowerDefinitions(fixture)[0].statistics().damage == TowerDefinition{}.statistics().damage,
            "legacy towers did not receive compatible combat defaults");
    save(towerPath, towerJson);
    require(loadLevelDefinition(levelPath).startingGold == 0 &&
                loadLevelDefinition(levelPath).waves[0].goldReward == 0 &&
                loadTowerDefinitions(fixture)[0].constructionCost == 0,
            "legacy economy defaults incorrect");
    for (const auto& invalid : nlohmann::json::array({-1, 1.5, true, "10", nullptr})) {
        auto modified = levelJson;
        modified["starting_gold"] = invalid;
        save(levelPath, modified);
        rejects([&] {
            (void)loadLevelDefinition(levelPath);
        });
        modified = levelJson;
        modified["waves"][0]["gold_reward"] = invalid;
        save(levelPath, modified);
        rejects([&] {
            (void)loadLevelDefinition(levelPath);
        });
        modified = towerJson;
        modified[0]["construction_cost"] = invalid;
        save(towerPath, modified);
        rejects([&] {
            (void)loadTowerDefinitions(fixture);
        });
    }
    auto excessive = level;
    excessive.startingGold = std::numeric_limits<std::uint64_t>::max();
    rejects([&] {
        validateLevelDefinition(excessive);
    });
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected asset root and fixture directory");
        transactions();
        configuration(argv[1], argv[2]);
        std::cout << "PASS economy: spending, earning, reset, overflow, authored costs/rewards, legacy defaults and "
                     "invalid JSON.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL economy: " << error.what() << '\n';
        return 1;
    }
}
