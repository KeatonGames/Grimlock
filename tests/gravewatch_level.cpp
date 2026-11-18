#include "runtime/level_catalog.hpp"
#include "runtime/level_runtime.hpp"
#include "ui/game_hud.hpp"

#include <imgui_internal.h>
#ifdef GRIMLOCK_TEST_EDITOR
#include "scene_document.hpp"
#endif
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace grimlock;
using namespace grimlock::game;

namespace {
void require(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

EnemyTypeId typeOf(EnemySystem& enemies, const std::string& name) {
    for (EnemyTypeId type = 0; type < enemies.typeCount(); ++type) {
        if (enemies.definition(type).name == name) {
            return type;
        }
    }
    throw std::runtime_error("Missing enemy: " + name);
}
} // namespace

int main() {
    try {
        Engine engine{EngineConfig{.windowTitle = "Gravewatch checks", .assetRoot = assetRootForLevel({})}};
        const auto root = engine.assetRoot();
        const auto definition = loadLevelDefinition(root / "levels/gravewatch_hollow.json");
        require(definition.waves.size() == 5 && definition.availableHeroes == std::vector<std::string>{"knight"},
                "Chapter roster/waves incorrect");
        const auto catalog = discoverLevels(root);
        require(catalog.errors.empty(), "Level catalog contains errors");
        const auto entry = std::find_if(catalog.levels.begin(), catalog.levels.end(), [&](const auto& level) {
            return level.id == definition.id;
        });
        require(entry != catalog.levels.end(), "Gravewatch missing from catalog");
        const auto progressPath = std::filesystem::temp_directory_path() / "grimlock-gravewatch-checks.json";
        std::filesystem::remove(progressPath);
        {
            PlayerProgress progress{progressPath};
            require(!levelUnlocked(*entry, &progress), "Chapter starts unlocked");
            progress.startAttempt(definition.requiredLevel);
            (void)progress.finishAttempt(definition.requiredLevel, true, 30);
        }
        PlayerProgress progress{progressPath};
        require(levelUnlocked(*entry, &progress), "Prerequisite completion did not persist");
        LevelRuntime runtime;
        runtime.attachProgress(progress);
        runtime.load(engine, root / "levels/gravewatch_hollow.json");
        auto& nav = runtime.navigation();
        auto& enemies = *nav.enemies();
        const auto field = engine.scene()->object(1).heightfield;
        require(field && field->sample({11, 12})->height - field->sample({-15, -14})->height > 1,
                "Chapter terrain is flat");
        require(nav.placement().zones().size() == 6, "Construction locations missing");
        require(runtime.heroes()->heroes().size() == 1, "Knight not deployed");
        const auto heroId = runtime.heroes()->heroes().front().id;
        const auto& knight = runtime.heroes()->definition(runtime.heroes()->heroes().front());
        require(knight.maximumHealth == 240 && knight.speed < 3.5 && knight.combat.blockReduction > 0,
                "Knight tank config missing");
        (void)runtime.heroes()->applyDamage(heroId, 40);
        require(runtime.heroes()->find(heroId)->health == 230, "Automatic shield did not reduce damage");
        (void)runtime.heroes()->applyDamage(heroId, 40);
        require(runtime.heroes()->find(heroId)->health == 190, "Shield ignored cooldown");
        runtime.heroes()->update(3.1, true);
        (void)runtime.heroes()->applyDamage(heroId, 40);
        require(runtime.heroes()->find(heroId)->health == 180, "Shield did not recover");
        for (const auto& zone : nav.placement().zones()) {
            const auto tower = zone.snap(nav.towers().definition("basic").instance());
            const auto error = nav.checkTower(engine, tower);
            require(error.empty(), "Construction rejected: " + error);
            const auto navigationRevision = nav.revision();
            const auto heroRevision = runtime.heroes()->navigation().revision();
            const auto built = nav.placeTower(engine, tower);
            runtime.update(engine, 0);
            require(nav.revision() == navigationRevision && runtime.heroes()->navigation().revision() == heroRevision,
                    "Reserved build site rebuilt navigation on placement");
            require(nav.removeTower(engine, built), "Tower cleanup failed");
        }
        const auto skeleton = typeOf(enemies, "Skeleton"), boss = typeOf(enemies, "Skeleton Captain");
        require(enemies.definition(skeleton).boneHealth == 5 && enemies.definition(skeleton).maximumHealth == 40,
                "Existing Skeleton changed");
        require(enemies.definition(boss).boss &&
                    enemies.definition(boss).visualHeight > enemies.definition(skeleton).visualHeight &&
                    enemies.definition(boss).model == enemies.definition(skeleton).model &&
                    enemies.definition(boss).boneHealth > 0,
                "Boss must be a larger reviving Skeleton");
        runtime.heroes()->clear();
        for (const auto type : {skeleton, boss}) {
            nav.reset(engine);
            require(nav.spawnAt(engine, type, definition.spawns.front().area, 1) == 1,
                    "Skeleton route spawn failed: " + enemies.definition(type).name);
            for (int frame = 0; frame < 4500 && !enemies.enemies().empty(); ++frame) {
                nav.update(engine, 1.0 / 30);
            }
            require(enemies.enemies().empty() && enemies.metrics().arrived == 1, "Crypt-to-keep route failed");
        }
        runtime.restart(engine);
        GameHud hud{engine, runtime};
        // Real strategy-mode campaign: four initial towers; spend earned gold on upgrades between waves.
        for (std::size_t index = 0; index < 4; ++index) {
            const auto zone = runtime.navigation().placement().zones()[index];
            (void)runtime.level().buildTower(engine,
                                             runtime.navigation(),
                                             zone.snap(runtime.navigation().towers().definition("basic").instance()));
        }
        for (int wave = 0; wave < 5; ++wave) {
            if (wave > 0) {
                std::vector<ObjectId> towers;
                for (const auto& tower : runtime.navigation().towers().towers()) {
                    towers.push_back(tower.object);
                }
                for (const auto tower : towers) {
                    if (runtime.level().upgradeError(runtime.navigation(), tower).empty()) {
                        runtime.level().upgradeTower(engine, runtime.navigation(), tower);
                    }
                }
            }
            runtime.startWave();
            if (wave == 0) {
                require(runtime.controlHeroSlot(0), "Knight direct control unavailable");
            }
            runtime.releaseHeroControl();
            bool sawBoss = false, sawBossBar = false;
            for (int frame = 0; frame < 6000 && runtime.level().waves()->phase() == LevelPhase::battle; ++frame) {
                for (const auto& enemy : runtime.navigation().enemies()->enemies()) {
                    engine.animation(*enemy.instance).update(1.0 / 30);
                }
                runtime.update(engine, 1.0 / 30);
                for (const auto& enemy : runtime.navigation().enemies()->enemies()) {
                    sawBoss = sawBoss || runtime.navigation().enemies()->definition(enemy.type).boss;
                }
                if (sawBoss && !sawBossBar) {
                    require(engine.update(), "Boss frame failed");
                    const auto* bar = ImGui::FindWindowByName("Boss Health");
                    sawBossBar = bar && bar->Active;
                }
            }
            if (runtime.level().waves()->phase() == LevelPhase::battle) {
                std::cout << "Stalled wave " << wave + 1 << " pending " << runtime.level().waves()->pending()
                          << std::endl;
                for (const auto& enemy : runtime.navigation().enemies()->enemies()) {
                    std::cout << "Enemy " << enemy.id << " position " << enemy.position.x << ',' << enemy.position.y
                              << ',' << enemy.position.z << " life " << static_cast<int>(enemy.life) << " health "
                              << enemy.health << " target " << static_cast<int>(enemy.combat.target.kind) << std::endl;
                }
            }
            require(runtime.level().waves()->phase() != LevelPhase::battle, "Wave stuck");
            require(runtime.level().waves()->phase() != LevelPhase::defeat,
                    "Strategy-mode defense lost on wave " + std::to_string(wave + 1));
            if (wave == 4) {
                require(sawBoss && sawBossBar, "Final wave did not display the boss health bar");
            }
            std::cout << "PASS strategy wave " << wave + 1 << std::endl;
        }
        require(runtime.level().waves()->phase() == LevelPhase::victory, "Campaign did not finish");
        PlayerProgress reloaded{progressPath};
        require(reloaded.find(definition.id) && reloaded.find(definition.id)->completed,
                "Chapter completion did not persist");
        runtime.unload(engine);
#ifdef GRIMLOCK_TEST_EDITOR
        const auto save = std::filesystem::temp_directory_path() / "grimlock-gravewatch-scene.json";
        editor::SceneDocument document{engine};
        document.open(root / definition.scene);
        document.save(save);
        const auto roundTrip = Scene::load(save);
        require(roundTrip->object(1).heightfield && roundTrip->objects().size() == engine.scene()->objects().size(),
                "Editor lost terrain or decorations");
        std::filesystem::remove(save);
#endif
        const auto levelSave = std::filesystem::temp_directory_path() / "grimlock-gravewatch-level.json";
        saveLevelDefinition(levelSave, definition);
        require(loadLevelDefinition(levelSave).requiredLevel == definition.requiredLevel,
                "Level save lost prerequisite");
        std::filesystem::remove(levelSave);
        std::filesystem::remove(progressPath);
        runtime.load(engine, root / "levels/forest_defense.json");
        require(runtime.heroes()->heroes().empty() && runtime.navigation().placement().zones().size() == 4,
                "Level 1 changed");
        runtime.unload(engine);
        std::cout << "PASS Gravewatch terrain, construction, routes, shield, control, five strategy waves, "
                     "progression, serialization and Level 1 load\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL Gravewatch: " << error.what() << '\n';
        return 1;
    }
}
