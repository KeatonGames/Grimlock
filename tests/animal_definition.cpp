#include "entities/enemy_definition.hpp"

#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

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
    require(rejected, "invalid animal configuration accepted");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected asset and fixture directories");
        const auto definitions = loadAnimalDefinitions(argv[1]);
        require(definitions.size() == 14 && definitions.front().model == "models/chicken.glb" &&
                    definitions.front().radius == 0.195 && definitions.front().visualHeight == 0.36F,
                "animal catalog/default settings incorrect");
        bool varied = false;
        for (const auto& d : definitions) {
            validateEnemyDefinition(d);
            require(std::filesystem::is_regular_file(std::filesystem::path{argv[1]} / d.model),
                    "unavailable GLB listed");
            varied = varied || d.radius != definitions.front().radius;
        }
        require(varied, "animal footprints were not configured per type");
        const auto& grunt = definitions[11];
        const auto& siege = definitions[12];
        const auto& hunter = definitions[13];
        require(grunt.name == "Grunt" && grunt.combat.objectiveFirst && grunt.model == definitions[0].model &&
                    siege.name == "Siege" && siege.maximumHealth > grunt.maximumHealth && siege.speed < grunt.speed &&
                    siege.combat.towerPriority > siege.combat.heroPriority && siege.combat.towerDamageModifier == 2 &&
                    hunter.name == "Hunter" && hunter.maximumHealth < grunt.maximumHealth &&
                    hunter.speed > grunt.speed && hunter.combat.heroPriority > hunter.combat.towerPriority,
                "archetype statistics or independent animal inheritance incorrect");
        auto invalid = definitions.front();
        invalid.combat.towerDamageModifier = std::numeric_limits<float>::infinity();
        rejects([&] {
            validateEnemyDefinition(invalid);
        });
        invalid = definitions.front();
        invalid.radius = 0;
        rejects([&] {
            validateEnemyDefinition(invalid);
        });
        invalid = definitions.front();
        invalid.speed = std::numeric_limits<double>::quiet_NaN();
        rejects([&] {
            validateEnemyDefinition(invalid);
        });
        invalid = definitions.front();
        invalid.height = 0;
        rejects([&] {
            validateEnemyDefinition(invalid);
        });
        for (const double invalidValue : {-1.0, std::numeric_limits<double>::quiet_NaN()}) {
            invalid = definitions.front();
            invalid.stoppingDistance = invalidValue;
            rejects([&] {
                validateEnemyDefinition(invalid);
            });
            invalid = definitions.front();
            invalid.retryInterval = invalidValue;
            rejects([&] {
                validateEnemyDefinition(invalid);
            });
            invalid = definitions.front();
            invalid.stuckTimeout = invalidValue;
            rejects([&] {
                validateEnemyDefinition(invalid);
            });
        }

        const std::filesystem::path fixture{argv[2]};
        std::filesystem::create_directories(fixture / "models");
        std::filesystem::create_directories(fixture / "config");
        std::filesystem::remove(fixture / "config/enemy_archetypes.json");
        std::ofstream{fixture / "models/chicken.glb"};
        const nlohmann::json entry{{"name", "Chicken"},
                                   {"model", "models/chicken.glb"},
                                   {"radius", 0.195},
                                   {"height", 0.45},
                                   {"visual_height", 0.36}};
        const auto save = [&](const nlohmann::json& document) {
            std::ofstream file{fixture / "config/animals.json"};
            file << document.dump();
        };
        auto document = nlohmann::json::array({entry});
        save(document);
        require(loadAnimalDefinitions(fixture).size() == 1, "valid animal configuration rejected");
        document[0]["stopping_distance"] = 0.45;
        document[0]["retry_interval"] = 1.5;
        document[0]["stuck_timeout"] = 3.0;
        document[0]["maximum_health"] = 140;
        save(document);
        const auto configured = loadAnimalDefinitions(fixture).front();
        require(configured.stoppingDistance == 0.45 && configured.retryInterval == 1.5 &&
                    configured.stuckTimeout == 3.0 && configured.maximumHealth == 140 && configured.despawnOnArrival,
                "AI configuration or despawn-on-arrival default incorrect");
        document[0]["maximum_health"] = 0;
        save(document);
        rejects([&] {
            (void)loadAnimalDefinitions(fixture);
        });
        document[0]["maximum_health"] = 140;
        document[0]["despawn_on_arrival"] = false;
        save(document);
        require(!loadAnimalDefinitions(fixture).front().despawnOnArrival, "arrival retention override ignored");
        auto unavailable = entry;
        unavailable["name"] = "Unavailable";
        unavailable["model"] = "models/not-installed.glb";
        document.push_back(unavailable);
        save(document);
        require(loadAnimalDefinitions(fixture).size() == 1, "missing assets were offered in the selector");
        document.push_back(entry);
        save(document);
        rejects([&] {
            (void)loadAnimalDefinitions(fixture);
        });
        document = nlohmann::json::array({entry});
        document[0]["model"] = "../chicken.glb";
        save(document);
        rejects([&] {
            (void)loadAnimalDefinitions(fixture);
        });
        document[0] = entry;
        document[0]["radius"] = -1;
        save(document);
        rejects([&] {
            (void)loadAnimalDefinitions(fixture);
        });
        save(nlohmann::json::array({entry}));
        const auto saveArchetypes = [&](const nlohmann::json& entries) {
            std::ofstream file{fixture / "config/enemy_archetypes.json"};
            file << entries.dump();
        };
        auto archetype = nlohmann::json{{"name", "Test"},
                                        {"animal", "Chicken"},
                                        {"maximum_health", 120},
                                        {"speed", 1.4},
                                        {"combat", {{"objective_first", true}, {"tower_damage_modifier", 2}}}};
        saveArchetypes(nlohmann::json::array({archetype}));
        const auto inherited = loadAnimalDefinitions(fixture);
        require(inherited.size() == 2 && inherited.back().model == inherited.front().model &&
                    inherited.back().radius == inherited.front().radius && inherited.back().combat.objectiveFirst &&
                    inherited.back().combat.towerDamageModifier == 2,
                "archetype presentation inheritance or policy override failed");
        archetype["combat"]["tower_damage_modifier"] = -1;
        saveArchetypes(nlohmann::json::array({archetype}));
        rejects([&] {
            (void)loadAnimalDefinitions(fixture);
        });
        archetype["combat"]["tower_damage_modifier"] = 2;
        archetype["animal"] = "Missing";
        saveArchetypes(nlohmann::json::array({archetype}));
        rejects([&] {
            (void)loadAnimalDefinitions(fixture);
        });
        archetype["animal"] = "Chicken";
        archetype["name"] = "Chicken";
        saveArchetypes(nlohmann::json::array({archetype}));
        rejects([&] {
            (void)loadAnimalDefinitions(fixture);
        });
        std::filesystem::remove(fixture / "config/enemy_archetypes.json");
        require(loadAnimalDefinitions(fixture).size() == 1, "legacy catalog requires archetype configuration");
        std::cout << "PASS animal catalog, archetypes, inherited presentation, modifiers and invalid configuration.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL animal definitions: " << error.what() << '\n';
        return 1;
    }
}
