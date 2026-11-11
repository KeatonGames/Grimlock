#include "assets/model_loader.hpp"
#include "runtime/level_catalog.hpp"
#include "systems/combat_system.hpp"
#include "ui/game_hud.hpp"

#ifdef GRIMLOCK_TEST_EDITOR
#include "scene_document.hpp"
#endif

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <imgui_internal.h>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>

using namespace grimlock;
using namespace grimlock::game;

namespace {
void require(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void verifyAssets(const std::filesystem::path& root) {
    std::size_t count = 0;
    for (const auto& file : std::filesystem::directory_iterator(root / "environment/platformer")) {
        if (file.path().extension() != ".glb") {
            continue;
        }
        const auto model = assets::loadModel(file.path());
        require(!model.meshes.empty() && !model.materials.empty(), "Empty platformer asset: " + file.path().string());
        if (file.path().filename().string().starts_with("block-grass")) {
            require(model.skins.empty() && model.animations.empty(), "Unexpected animated terrain asset");
        }
        (void)assets::modelNodeTransforms(model);
        ++count;
    }
    require(count == 147, "Expected all 147 Platformer Kit environment GLBs");
    std::cout << "PASS all " << count << " Platformer Kit environment assets imported.\n";
}

void traversal(Engine& engine) {
    // Exercise the shipped GLB terrain rather than a simplified replacement ramp.
    for (int reload = 0; reload < 2; ++reload) {
        LevelRuntime runtime;
        runtime.load(engine, "assets/levels/forest_defense.json");
        auto& navigation = runtime.navigation();
        HeroSystem heroes{engine};
        heroes.synchronize(navigation.towers());
        const glm::dvec3 start{5.5, 0, -4.6};
        for (std::size_t type = 0; type < heroes.typeCount(); ++type) {
            for (const int fps : {15, 60, 144}) {
                const auto id = heroes.deploy(start, {}, type);
                for (const auto goal : {glm::dvec3{7, 1, 4.6}, start, glm::dvec3{-8.5, 2, 10.8}, start}) {
                    require(heroes.command(id, goal), "Forest hero ramp command rejected");
                    for (int frame = 0; frame < fps * 40 && heroes.find(id)->state == HeroState::moving; ++frame) {
                        const auto before = heroes.find(id)->position;
                        heroes.update(1.0 / fps, true);
                        const auto* hero = heroes.find(id);
                        require(heroes.navigation(type).contains(hero->position), "Hero left a ramp or bridge surface");
                        require(std::abs(hero->position.y - before.y) < heroes.definition(type).speed / fps + 0.2,
                                "Hero snapped between terrain elevations");
                    }
                    require(heroes.find(id)->state == HeroState::reached,
                            "Forest hero failed to traverse a ramp/bridge in both directions at " +
                                std::to_string(fps) + " FPS");
                }
                require(heroes.remove(id), "Traversal hero cleanup failed");
            }
        }
        auto& enemies = *navigation.enemies();
        for (const int fps : {15, 60, 144}) {
            navigation.reset(engine);
            navigation.setDestination(glm::dvec2{-8.5, 10.8});
            navigation.setDestinationHeight(2);
            for (const std::string name : {"Goblin", "Skeleton", "Goblin Boss"}) {
                EnemyTypeId type = 0;
                while (type < enemies.typeCount() && enemies.definition(type).name != name) {
                    ++type;
                }
                require(type < enemies.typeCount(), "Traversal enemy archetype missing");
                require(navigation.spawnAt(engine, type, {{3, -5}, {10, -1}, 0}, 12) == 12,
                        "Mixed traversal group did not fit on entrance terrain");
            }
            for (int frame = 0; frame < fps * 120 && !enemies.enemies().empty(); ++frame) {
                navigation.update(engine, 1.0 / fps);
                for (const auto& enemy : enemies.enemies()) {
                    require(enemy.navigation->contains(enemy.position), "Mixed crowd left its own terrain profile");
                }
            }
            require(enemies.enemies().empty() && enemies.metrics().arrived == 36,
                    "Mixed Goblin/Skeleton/boss crowd stuck on the Forest ramps or bridge");
        }
        heroes.clear();
        runtime.unload(engine);
    }
}

void terrain(Engine& engine) {
    LevelRuntime runtime;
    runtime.load(engine, "assets/levels/forest_defense.json");
    require(!engine.scene()->navigationGrid(), "Forest depends on a debug grid");
    require(runtime.level().waves()->definition().waves.size() == 5, "Forest requires five handcrafted waves");
    const auto& waves = runtime.level().waves()->definition().waves;
    for (std::size_t wave = 0; wave < 4; ++wave) {
        for (const auto& group : waves[wave].groups) {
            require(group.enemy == "Goblin" && group.batchSize == 1 && group.interval == 0.6,
                    "Introductory Goblins must spawn individually at 0.6-second intervals");
        }
    }
    const auto& finale = waves.back().groups;
    require(finale.size() == 2 && finale[0].enemy == "Goblin Boss" && finale[0].count == 1 &&
                finale[0].batchSize == 1 && finale[0].start == 0 && finale[1].enemy == "Goblin" &&
                finale[1].count == 4 && finale[1].batchSize == 1 && finale[1].start == 0.6 && finale[1].interval == 0.6,
            "Final wave must introduce the boss followed by four individual Goblins");
    const auto catalog = discoverLevels(engine.assetRoot());
    require(std::any_of(catalog.levels.begin(),
                        catalog.levels.end(),
                        [&](const auto& entry) {
                            return entry.id == runtime.level().waves()->definition().id;
                        }),
            "Forest is missing from Game discovery");
    auto& navigation = runtime.navigation();
    require(navigation.placement().zones().size() == 4, "Forest construction zones missing");
    const std::array<glm::dvec2, 4> lanes{{{5, -1.5}, {3, 0}, {-5, 0.5}, {-7, 7.5}}};
    for (std::size_t index = 0; index < lanes.size(); ++index) {
        const auto& zone = navigation.placement().zones()[index];
        require(zone.object == 108 + index && zone.constructionObject != 0,
                "Forest zone is missing its authored construction facing");
        const auto foot = zone.constructionPosition();
        const auto expected = glm::normalize(glm::dvec3{lanes[index].x - foot.x, 0, lanes[index].y - foot.z});
        for (const auto* type : {"basic", "wide"}) {
            const auto tower = zone.snap(navigation.towers().definition(type).instance());
            require(glm::dot(tower.rotation * glm::dvec3{0, 0, 1}, expected) > 0.9999,
                    "Tower does not face the zone's level-specific enemy lane");
        }
    }
    NavigationPath path;
    require(navigation.navigation().findPath({10, 0, -3.5}, {-8.5, 2, 10.8}, path),
            "Forest spawn cannot reach hill objective");
    require(navigation.navigation().findPath({5.5, 0, -4.6}, {7, 1, 4.6}, path), "Lookout ramp is disconnected");
    require(!navigation.navigation().contains({0, 0, 4}), "Navigation crossed the ravine beside the bridge");
    require(runtime.heroes()->typeCount() == 0 && runtime.heroes()->heroes().empty(), "Level 1 made heroes available");
    EnemyTypeId bossType = 0;
    while (bossType < navigation.enemies()->typeCount() &&
           navigation.enemies()->definition(bossType).name != "Goblin Boss") {
        ++bossType;
    }
    require(bossType < navigation.enemies()->typeCount(), "Forest boss definition missing");
    const auto& bossDefinition = navigation.enemies()->definition(bossType);
    require(bossDefinition.boss && bossDefinition.model == "enemies/orcs/character-orc.glb" &&
                bossDefinition.maximumHealth == 900 && bossDefinition.visualHeight == 1.5,
            "Goblin boss did not use the animated Goblin model and boss statistics");
    require(navigation.surface(engine, bossType).contains({0, 0, 0}) &&
                !navigation.surface(engine, bossType).contains({0, 0, 4}),
            "Bridge deck does not support the boss independently of the ravine");
    require(navigation.spawnAt(engine, bossType, {{10, -3.5}, {10, -3.5}, 0}, 1) == 1,
            "Forest boss clearance spawn failed");
    bool bossClimbed = false;
    for (int step = 0; step < 2400 && !navigation.enemies()->enemies().empty(); ++step) {
        navigation.update(engine, 1.0 / 30);
        for (const auto& enemy : navigation.enemies()->enemies()) {
            require(enemy.navigation->contains(enemy.position), "Forest boss left its radius-aware surface");
            bossClimbed = bossClimbed || enemy.position.y > 1;
        }
    }
    require(bossClimbed && navigation.enemies()->metrics().arrived == 1,
            "Forest boss could not traverse the full ascent");
    navigation.reset(engine);
    runtime.beginPlacement("wide");
    const Ray hill{{-5.8, 12, 10.8}, {0, -1, 0}, 0, 30};
    runtime.updatePlacement(engine, hill, false);
    require(runtime.preview() && runtime.placementError().empty(), "Hill placement: " + runtime.placementError());
    require(std::abs(runtime.preview()->position.y - runtime.preview()->halfExtent.y - 2) < 1e-5,
            "Tower foundation ignored hill elevation");
    runtime.updatePlacement(engine, hill, true);
    require(navigation.towers().towers().size() == 1 && runtime.level().economy().gold() == 5,
            "Game hill placement did not commit tower/cost");
    require(engine.update(), "Forest rendering failed");
    std::set<std::filesystem::path> referenced;
    for (const auto& [id, object] : engine.scene()->objects()) {
        if (object.model) {
            referenced.insert(*object.model);
        }
    }
    for (std::size_t type = 0; type < runtime.heroes()->typeCount(); ++type) {
        referenced.insert(runtime.heroes()->definition(type).model);
    }
    referenced.insert(navigation.enemies()->definition(bossType).model);
    require(engine.performance().models == referenced.size(),
            "Repeated forest instances duplicated GPU model resources");
    navigation.synchronize(engine);
    require(navigation.spawnAt(engine, 0, {{-6.5, 8}, {-6.5, 8}, 1.75}, 1) == 1,
            "Forest ramp combat target could not spawn");
    navigation.enemies()->setSpeed(0, 0);
    CombatSystem combat{engine};
    for (int step = 0; step < 60 && !navigation.enemies()->enemies().empty(); ++step) {
        combat.update(navigation.towers(), *navigation.enemies(), 0.1, &navigation.combatGeometry());
        navigation.enemies()->updateEffects(0.1);
    }
    require(navigation.enemies()->metrics().defeated == 1 && combat.projectiles().impacts() >= 2,
            "Forest hill tower failed to shoot a lower target across the ramp");
    const auto saved = engine.assetRoot().parent_path() / "forest-roundtrip.json";
    engine.saveScene(saved);
    const auto roundtrip = engine.loadScene(saved);
    require(roundtrip->objects().size() == engine.scene()->objects().size() && !roundtrip->navigationGrid(),
            "Forest scene roundtrip lost authored objects");
    runtime.unload(engine);
#ifdef GRIMLOCK_TEST_EDITOR
    editor::SceneDocument document{engine};
    document.open(engine.assetRoot() / "scenes/forest_defense.json");
    document.refreshModels();
    const auto expected = std::filesystem::path{"environment/platformer/tree-pine.glb"};
    require(std::find(document.models().begin(), document.models().end(), expected) != document.models().end(),
            "Editor did not discover nested Platformer Kit assets");
    ObjectId tree = 0;
    for (const auto& [id, object] : engine.scene()->objects()) {
        if (object.model == expected) {
            tree = id;
            break;
        }
    }
    require(tree != 0, "Editable forest tree missing");
    const auto duplicate = document.duplicate(tree);
    auto transform = engine.scene()->object(duplicate).local;
    transform.position.x += 0.5F;
    document.setTransform(duplicate, transform);
    document.save(engine.assetRoot().parent_path() / "forest-editor-roundtrip.json");
    require(engine.scene()->object(duplicate).role == "grimlock.decoration", "Editor duplication changed scene roles");
#endif
}

void play(Engine& engine, bool defended) {
    const auto progressPath =
        engine.assetRoot().parent_path() / (defended ? "forest-victory-progress.json" : "forest-defeat-progress.json");
    std::filesystem::remove(progressPath);
    PlayerProgress progress{progressPath};
    LevelRuntime runtime;
    runtime.attachProgress(progress);
    runtime.load(engine, "assets/levels/forest_defense.json");
    GameHud hud{engine, runtime};
    auto& session = runtime.level();
    auto& navigation = runtime.navigation();
    auto& heroes = *runtime.heroes();

    ObjectId lastStand = 0;
    const auto build = [&](const char* type, glm::dvec3 ground) {
        auto tower = navigation.towers().definition(type).instance();
        const auto* zone = navigation.placement().zoneAt(ground);
        require(zone != nullptr, "Forest strategy has no construction point");
        tower = zone->snap(tower);
        require(heroes.towerError(tower).empty(), "Forest strategy overlaps a hero");
        const auto error = navigation.checkTower(engine, tower, false);
        require(error.empty(),
                "Forest build " + std::string{type} + " at " + std::to_string(ground.x) + "," +
                    std::to_string(ground.y) + "," + std::to_string(ground.z) + ": " + error);
        return session.buildTower(engine, navigation, tower);
    };
    if (defended) {
        (void)build("basic", {5.5, 0, -4.6});
        lastStand = build("basic", {-5.8, 2, 10.8});
    }

    bool bossSeen = false, bossDefeated = false, bossBarSeen = false;
    std::optional<EnemyId> boss;
    while (session.waves()->phase() == LevelPhase::preparation) {
        const auto wave = session.waves()->wave();
        if (defended && wave == 1) {
            (void)build("basic", {-7.5, 0, -2.4});
        }
        if (defended && wave >= 3) {
            const auto tower = lastStand;
            if (navigation.towers().contains(tower) && session.upgradeError(navigation, tower).empty()) {
                session.upgradeTower(engine, navigation, tower);
            }
        }
        const auto gold = session.economy().gold();
        const auto killGold = navigation.enemies()->metrics().killGold;
        runtime.startWave();
        double time = 0;
        while (session.waves()->phase() == LevelPhase::battle && time < 180) {
            const auto arrivals = navigation.enemies()->metrics().arrived;
            const bool trackingBoss = boss && navigation.enemies()->find(*boss);
            const auto pending = session.waves()->pending();
            runtime.update(engine, 1.0 / 30);
            require(pending - session.waves()->pending() <= 1, "Forest spawned multiple enemies together");
            if (trackingBoss && !navigation.enemies()->find(*boss)) {
                bossDefeated = navigation.enemies()->metrics().arrived == arrivals;
            }
            for (const auto& enemy : navigation.enemies()->enemies()) {
                require(enemy.navigation->contains(enemy.position), "Forest enemy escaped navigation");
                const auto& name = navigation.enemies()->definition(enemy.type).name;
                require(name == "Goblin" || name == "Goblin Boss", "Level 1 spawned another enemy archetype");

                if (navigation.enemies()->definition(enemy.type).boss) {
                    require(wave == 4 && name == "Goblin Boss", "Wrong boss or boss spawned before wave five");
                    bossSeen = true;
                    boss = enemy.id;
                }
            }
            time += 1.0 / 30;
            if (static_cast<int>(time * 30) % 60 == 0) {
                require(engine.update(), "Forest Game HUD/rendering failed");
                if (boss && navigation.enemies()->find(*boss)) {
                    const auto* bar = ImGui::FindWindowByName("Boss Health");
                    require(bar && bar->Active, "Forest boss did not activate the Game health bar");
                    bossBarSeen = true;
                }
            }
        }
        std::cout << "Forest " << defended << " wave " << wave + 1 << ": " << levelPhaseName(session.waves()->phase())
                  << "; time " << time << "; health " << session.waves()->health() << "; gold "
                  << session.economy().gold() << "; enemies " << navigation.enemies()->enemies().size() << std::endl;
        if (session.waves()->phase() == LevelPhase::battle) {
            for (const auto& enemy : navigation.enemies()->enemies()) {
                std::cout << "Stalled " << enemy.id << " at " << enemy.position.x << ',' << enemy.position.y << ','
                          << enemy.position.z << " target " << static_cast<int>(enemy.combat.target.kind) << " type "
                          << navigation.enemies()->definition(enemy.type).name << " life "
                          << static_cast<int>(enemy.life) << " health " << enemy.health << '\n';
            }
        }
        require(session.waves()->phase() != LevelPhase::battle, "Forest wave failed to finish");
        require(session.waves()->definition().waves[wave].goldReward == 0, "Forest still awards wave bonuses");
        const auto earned = navigation.enemies()->metrics().killGold - killGold;
        require(session.economy().gold() == gold + earned, "Forest wave or kill rewards are incorrect");
        runtime.update(engine, 0);
        require(session.economy().gold() == gold + earned, "Forest reward was granted twice");
    }
    require(session.waves()->phase() == (defended ? LevelPhase::victory : LevelPhase::defeat),
            "Forest does not distinguish defended and undefended strategies");
    if (defended) {
        require(session.waves()->wave() == 5 && bossSeen && bossDefeated && bossBarSeen,
                "Final forest boss or its health bar was not exercised");
        require(navigation.enemies()->metrics().defeated + navigation.enemies()->metrics().arrived == 45,
                "Forest skipped scheduled enemies");
        require(navigation.enemies()->metrics().killGold == 90,
                "The 45 Goblins, including the boss, did not each award two coins");
    }
    const auto levelId = session.waves()->definition().id;
    PlayerProgress saved{progressPath};
    const auto* record = saved.find(levelId);
    require(saved.error().empty() && record && record->attempts == 1 && record->victories == (defended ? 1 : 0) &&
                record->completed == defended && saved.lastPlayed() == levelId,
            "Forest results did not persist through the shared progression system");
    runtime.restart(engine);
    require(session.waves()->phase() == LevelPhase::preparation && session.economy().gold() == 20 &&
                navigation.towers().towers().empty() && runtime.heroes()->heroes().empty(),
            "Forest restart retained runtime state");
    runtime.unload(engine);
    require(engine.update() && engine.performance().instances == 0, "Forest unload retained render instances");
}
} // namespace

int main(int argc, char** argv) {
    try {
        Engine engine{EngineConfig{.windowTitle = "Forest Defense checks", .width = 1280, .height = 800}};
        engine.setPerformanceEnabled(true);
        if (argc == 2 && std::string_view{argv[1]} == "--preview") {
            LevelRuntime runtime;
            runtime.load(engine, "assets/levels/forest_defense.json");
            GameHud hud{engine, runtime};
            while (engine.update([&](double delta) {
                runtime.update(engine, delta);
            })) {
            }
            return 0;
        }
        verifyAssets(engine.assetRoot());
        terrain(engine);
        play(engine, true);
        play(engine, false);
        traversal(engine);
        std::cout << "PASS Forest Defense assets, navigation, elevation, placement, five waves, boss, editor and "
                     "lifecycle.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Forest Defense: " << error.what() << '\n';
        return 1;
    }
}
