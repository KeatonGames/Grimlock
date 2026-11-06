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
    for (const auto& file : std::filesystem::directory_iterator(root / "environment/nature/kenney")) {
        if (file.path().extension() != ".glb") {
            continue;
        }
        const auto model = assets::loadModel(file.path());
        require(!model.meshes.empty() && !model.materials.empty(), "Empty nature asset: " + file.path().string());
        require(model.skins.empty() && model.animations.empty(), "Unexpected animated terrain asset");
        (void)assets::modelNodeTransforms(model);
        ++count;
    }
    require(count == 329, "Expected all 329 supplied Nature Kit GLBs");
    std::cout << "PASS all " << count << " original Nature Kit assets imported.\n";
}

void terrain(Engine& engine) {
    LevelRuntime runtime;
    runtime.load(engine, "assets/levels/forest_defense.json");
    require(!engine.scene()->navigationGrid(), "Forest depends on a debug grid");
    require(runtime.level().waves()->definition().waves.size() == 5, "Forest requires five handcrafted waves");
    const auto catalog = discoverLevels(engine.assetRoot());
    require(std::any_of(catalog.levels.begin(),
                        catalog.levels.end(),
                        [&](const auto& entry) {
                            return entry.id == runtime.level().waves()->definition().id;
                        }),
            "Forest is missing from Game discovery");
    auto& navigation = runtime.navigation();
    require(navigation.placement().zones().size() == 5, "Forest construction zones missing");
    NavigationPath path;
    require(navigation.navigation().findPath({-10, 0, -4}, {4, 2, 12}, path),
            "Forest spawn cannot reach hill objective");
    require(navigation.navigation().findPath({0, 0, 4}, {-6.5, 1, 4.2}, path), "Lookout ramp is disconnected");
    require(!navigation.navigation().contains({0, 0, -4}), "Navigation crossed the missing meadow corner");
    for (std::size_t type = 0; type < runtime.heroes()->typeCount(); ++type) {
        auto& heroes = *runtime.heroes();
        heroes.clear();
        const auto id = heroes.deploy({0, 0, 0}, {}, type);
        require(heroes.command(id, {4, 2, 12}), "Hero cannot reach elevated objective");
        bool climbed = false;
        for (int step = 0; step < 1500; ++step) {
            heroes.update(1.0 / 60, true);
            const auto* hero = heroes.find(id);
            require(hero && heroes.navigation(type).contains(hero->position), "Hero escaped forest terrain");
            climbed = climbed || (hero->position.y > 0.4 && hero->position.y < 1.7);
        }
        require(climbed && heroes.find(id)->state == HeroState::reached && heroes.find(id)->position.y > 1.9,
                "Hero did not arrive on the watch hill");
    }
    runtime.heroes()->clear();
    EnemyTypeId bossType = 0;
    while (bossType < navigation.enemies()->typeCount() && !navigation.enemies()->definition(bossType).boss) {
        ++bossType;
    }
    require(bossType < navigation.enemies()->typeCount(), "Forest boss definition missing");
    require(navigation.spawnAt(engine, bossType, {{-10, -4}, {-10, -4}, 0}, 1) == 1,
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
    const Ray hill{{6.8, 12, 10.5}, {0, -1, 0}, 0, 30};
    runtime.updatePlacement(engine, hill, false);
    require(runtime.preview() && runtime.placementError().empty(), "Hill placement: " + runtime.placementError());
    require(std::abs(runtime.preview()->position.y - runtime.preview()->halfExtent.y - 2) < 1e-5,
            "Tower foundation ignored hill elevation");
    runtime.updatePlacement(engine, hill, true);
    require(navigation.towers().towers().size() == 1 && runtime.level().economy().gold() == 60,
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
    require(navigation.spawnAt(engine, 0, {{4.8, 7.5}, {4.8, 7.5}, 1.5}, 1) == 1,
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
    const auto expected = std::filesystem::path{"environment/nature/kenney/tree_default.glb"};
    require(std::find(document.models().begin(), document.models().end(), expected) != document.models().end(),
            "Editor did not discover nested Nature Kit assets");
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
    const auto assassin = heroes.heroes().front().id;
    ObjectId west = 0, center = 0;
    const auto build = [&](const char* type, glm::dvec3 ground) {
        auto tower = navigation.towers().definition(type).instance();
        tower.position = ground + glm::dvec3{0, tower.halfExtent.y, 0};
        require(heroes.towerError(tower).empty(), "Forest strategy overlaps a hero");
        const auto error = navigation.checkTower(engine, tower, false);
        require(error.empty(),
                "Forest build " + std::string{type} + " at " + std::to_string(ground.x) + "," +
                    std::to_string(ground.y) + "," + std::to_string(ground.z) + ": " + error);
        return session.buildTower(engine, navigation, tower);
    };
    if (defended) {
        (void)heroes.deploy({-6, 0, -2}, assassin);
        (void)heroes.deploy({2, 0, 0}, {}, 1);
        (void)heroes.deploy({-4, 0, 0}, {}, 2);
        west = build("basic", {-8, 0, -2});
        center = build("wide", {0, 0, 0});
        (void)build("basic", {6.8, 2, 10.5});
    }
    HeroCombatSystem abilities;
    bool bossSeen = false, bossDefeated = false, bossBarSeen = false;
    std::optional<EnemyId> boss;
    while (session.waves()->phase() == LevelPhase::preparation) {
        const auto wave = session.waves()->wave();
        if (defended && wave > 0) {
            const auto tower = wave == 1 || wave == 3 ? west : center;
            if (navigation.towers().contains(tower) && session.upgradeError(navigation, tower).empty()) {
                session.upgradeTower(engine, navigation, tower);
            }
        }
        const auto gold = session.economy().gold();
        runtime.startWave();
        double time = 0;
        while (session.waves()->phase() == LevelPhase::battle && time < 180) {
            if (defended) {
                const auto* hero = heroes.find(assassin);
                if (hero && hero->health > 0) {
                    std::size_t near = 0;
                    for (const auto& enemy : navigation.enemies()->enemies()) {
                        near += glm::length(enemy.position - hero->position) <
                                heroes.definition(*hero).combat.cleave.radius;
                    }
                    if (near >= 2) {
                        (void)abilities.activate(heroes, assassin, HeroAbility::cleave, true);
                    }
                }
            }
            const auto arrivals = navigation.enemies()->metrics().arrived;
            const bool trackingBoss = boss && navigation.enemies()->find(*boss);
            runtime.update(engine, 1.0 / 30);
            if (trackingBoss && !navigation.enemies()->find(*boss)) {
                bossDefeated = navigation.enemies()->metrics().arrived == arrivals;
            }
            for (const auto& enemy : navigation.enemies()->enemies()) {
                require(enemy.navigation->contains(enemy.position), "Forest enemy escaped navigation");
                if (navigation.enemies()->definition(enemy.type).boss) {
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
                          << enemy.position.z << " target " << static_cast<int>(enemy.combat.target.kind) << '\n';
            }
        }
        require(session.waves()->phase() != LevelPhase::battle, "Forest wave failed to finish");
        const auto reward =
            session.waves()->phase() == LevelPhase::defeat ? 0 : session.waves()->definition().waves[wave].goldReward;
        require(session.economy().gold() == gold + reward, "Forest wave reward changed or repeated");
        runtime.update(engine, 0);
        require(session.economy().gold() == gold + reward, "Forest reward was granted twice");
    }
    require(session.waves()->phase() == (defended ? LevelPhase::victory : LevelPhase::defeat),
            "Forest does not distinguish defended and undefended strategies");
    if (defended) {
        require(session.waves()->wave() == 5 && bossSeen && bossDefeated && bossBarSeen,
                "Final forest boss or its health bar was not exercised");
        require(navigation.enemies()->metrics().defeated + navigation.enemies()->metrics().arrived == 51,
                "Forest skipped scheduled enemies");
    }
    const auto levelId = session.waves()->definition().id;
    PlayerProgress saved{progressPath};
    const auto* record = saved.find(levelId);
    require(saved.error().empty() && record && record->attempts == 1 && record->victories == (defended ? 1 : 0) &&
                record->completed == defended && saved.lastPlayed() == levelId,
            "Forest results did not persist through the shared progression system");
    runtime.restart(engine);
    require(session.waves()->phase() == LevelPhase::preparation && session.economy().gold() == 120 &&
                navigation.towers().towers().empty() && runtime.heroes()->heroes().size() == 1,
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
        std::cout << "PASS Forest Defense assets, navigation, elevation, placement, five waves, boss, editor and "
                     "lifecycle.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Forest Defense: " << error.what() << '\n';
        return 1;
    }
}
