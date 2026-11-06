#include "runtime/level_runtime.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace grimlock;
using namespace grimlock::game;

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void checkZones(Engine& engine) {
    LevelRuntime runtime;
    runtime.load(engine, "assets/levels/first_watch.json");
    auto& navigation = runtime.navigation();
    auto zone = navigation.placement().zones().front();
    zone.constructionOffset = {0.15, 0, 0.2};
    zone.constructionYaw = 0.35;
    PlacementSystem::editZone(*engine.scene(), zone);
    navigation.synchronize(engine);
    auto preview = navigation.towerAt({zone.position + glm::dvec3{0.6, 20, 0.5}, {0, -1, 0}, 0, 40});
    require(preview && glm::length(preview->position - zone.snap(Tower{}).position) < 1e-5,
            "cursor did not snap to the authored offset");
    const auto gold = runtime.level().economy().gold();
    const auto id = runtime.level().buildTower(engine, navigation, *preview);
    const auto entity = navigation.towers().entityForObject(id);
    require(navigation.placement().zones().front().occupants == std::vector<ecs::Entity>{entity},
            "construction did not claim the zone with an ECS handle");
    require(runtime.level().economy().gold() == gold - navigation.towers().definition("basic").constructionCost,
            "successful construction charged incorrectly");
    const auto afterBuild = runtime.level().economy().gold();
    bool rejected = false;
    try {
        (void)runtime.level().buildTower(engine,
                                         navigation,
                                         zone.snap(navigation.towers().definition("wide").instance()));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && runtime.level().economy().gold() == afterBuild && navigation.towers().towers().size() == 1,
            "occupied zone accepted another type or spent gold on rejection");

    const auto saved = engine.assetRoot().parent_path() / "unique-zone-roundtrip.json";
    engine.scene()->save(saved);
    engine.setScene(engine.loadScene(saved));
    navigation.synchronize(engine);
    require(navigation.placement().zones().front().occupants.size() == 1 &&
                navigation.towers().towers().front().buildZone == zone.object &&
                glm::length(navigation.towers().towers().front().position - preview->position) < 1e-5 &&
                glm::length(navigation.towers().towers().front().rotation - preview->rotation) < 1e-5,
            "scene round trip lost occupancy, construction point or facing");
    const auto restoredEntity = navigation.towers().entityForObject(id);
    require(navigation.towers().applyDamage(engine, id, 10000), "tower destruction failed");
    navigation.synchronize(engine);
    require(!navigation.towers().find(restoredEntity) && navigation.placement().zones().front().occupants.empty(),
            "death retained a stale entity or occupied zone");
    auto replacement = zone.snap(navigation.towers().definition("wide").instance());
    const auto second = navigation.placeTower(engine, replacement, PlacementMode::authoring);
    require(navigation.towers().entityForObject(second) != restoredEntity &&
                navigation.placement().zones().front().occupants.size() == 1,
            "entity slot reuse corrupted occupancy");

    runtime.restart(engine);
    require(navigation.placement().zones().front().occupants.empty() && navigation.towers().towers().empty(),
            "restart retained occupancy");
    runtime.load(engine, "assets/levels/hill_watch.json");
    require(std::all_of(navigation.placement().zones().begin(),
                        navigation.placement().zones().end(),
                        [](const BuildZone& item) {
                            return item.occupants.empty();
                        }),
            "switching levels retained occupancy");
}
} // namespace

int main() {
    try {
        Engine engine{EngineConfig{.windowTitle = "Unique construction zone checks"}};
        checkZones(engine);
        std::cout
            << "PASS fixed point, facing, occupancy, economy, save/load, death, entity reuse, restart and switch.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL unique zones: " << error.what() << '\n';
        return 1;
    }
}
