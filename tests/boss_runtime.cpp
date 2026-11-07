#include "runtime/level_runtime.hpp"
#include "systems/enemy_combat_system.hpp"
#include "ui/game_hud.hpp"

#include <algorithm>
#include <cmath>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>

using namespace grimlock;
using namespace grimlock::game;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

EnemyTypeId bossType(const EnemySystem& enemies) {
    for (EnemyTypeId type = 0; type < enemies.typeCount(); ++type) {
        if (enemies.definition(type).boss) {
            return type;
        }
    }
    throw std::runtime_error("boss definition missing");
}

void clearance(const EnemyDefinition& boss) {
    NavigationGeometry geometry;
    geometry.addBox({-6, -0.5F, -5}, {6, 0, 5}, true);
    geometry.addBox({-0.3F, 0, -5}, {0.3F, 3, -0.6F}, false);
    geometry.addBox({-0.3F, 0, 0.6F}, {0.3F, 3, 5}, false);
    NavMesh mesh;
    NavigationPath path;
    require(mesh.build(geometry) && mesh.findPath({-3, 0, 0}, {3, 0, 0}, path), "narrow passage did not admit minion");
    auto settings = mesh.configuration();
    settings.agentRadius = static_cast<float>(boss.radius);
    settings.agentHeight = static_cast<float>(boss.height);
    require(mesh.build(geometry, settings) && !mesh.findPath({-3, 0, 0}, {3, 0, 0}, path),
            "boss fit through a passage narrower than its collision diameter");
}

void combat(Engine& engine) {
    auto scene = std::make_unique<Scene>("Boss combat");
    scene->setNavigationGrid(GridConfiguration{30, 20, 1, {-15, 0, -10}});
    engine.setScene(std::move(scene));
    NavigationSession navigation;
    navigation.synchronize(engine);
    navigation.setDestination(glm::dvec2{12, 0});
    auto tower = navigation.towers().definition("basic").instance();
    tower.position = {0, tower.halfExtent.y, 0};
    const auto towerId = navigation.towers().create(engine, tower);
    navigation.synchronize(engine);
    HeroSystem heroes{engine};
    heroes.synchronize(navigation.towers());
    const auto hero = heroes.deploy({-3, 0, 2});
    const auto type = bossType(*navigation.enemies());
    require(navigation.spawnAt(engine, type, {{-3, 0}, {-3, 0}, 0}, 1) == 1, "boss spawn failed");
    auto& enemies = *navigation.enemies();
    const auto id = enemies.enemies().front().id;
    const auto& definition = enemies.definition(type);
    require(definition.maximumHealth > enemies.definition().maximumHealth &&
                definition.combat.damage > enemies.definition().combat.damage &&
                definition.radius > enemies.definition().radius,
            "boss was not stronger/larger than a minion");
    clearance(definition);
    EnemyCombatSystem attacks;
    const auto tick = [&] {
        attacks.prepare(enemies, navigation.towers(), &heroes, {12, 0}, 0.1, &navigation.combatGeometry());
        navigation.update(engine, 0.1);
        attacks.resolve(engine, enemies, navigation.towers(), &heroes, 0.1);
    };
    const float initialTowerHealth = navigation.towers().progress(towerId).health;
    for (int frame = 0; frame < 300 && navigation.towers().progress(towerId).health == initialTowerHealth; ++frame) {
        tick();
        require(enemies.find(id)->navigation->contains(enemies.find(id)->position), "boss left obstacle-cleared mesh");
    }
    require(navigation.towers().progress(towerId).health ==
                initialTowerHealth - definition.combat.damage * definition.combat.towerDamageModifier,
            "boss did not damage tower");
    require(navigation.removeTower(engine, towerId), "boss tower cleanup failed");
    heroes.synchronize(navigation.towers());
    for (int frame = 0; frame < 300 && heroes.find(hero)->health == 100; ++frame) {
        tick();
    }
    require(heroes.find(hero)->health == 100 - definition.combat.damage, "boss did not attack hero");
    attacks.stop(enemies);
    HeroCombatSystem heroCombat;
    const float health = enemies.find(id)->health;
    for (int frame = 0; frame < 300 && enemies.find(id)->health == health; ++frame) {
        heroCombat.update(heroes, enemies, 0.1, true);
    }
    require(enemies.find(id)->health < health, "hero could not damage boss");
    for (int frame = 0; frame < 1200 && enemies.find(id); ++frame) {
        heroCombat.update(heroes, enemies, 0.1, true);
    }
    require(!enemies.find(id) && enemies.metrics().defeated == 1, "hero could not kill boss");
    require(navigation.spawnAt(engine, type, {{-3, 0}, {-3, 0}, 0}, 1) == 1, "tower boss spawn failed");
    const auto towerTarget = enemies.enemies().front().id;
    auto defender = navigation.towers().definition("wide").instance();
    defender.position = {-5, defender.halfExtent.y, 0};
    (void)navigation.towers().create(engine, defender);
    navigation.synchronize(engine);
    CombatSystem towerCombat{engine};
    for (int frame = 0; frame < 1200 && enemies.find(towerTarget); ++frame) {
        towerCombat.update(navigation.towers(), enemies, 0.1, &navigation.combatGeometry());
    }
    require(!enemies.find(towerTarget) && enemies.metrics().defeated == 2 && towerCombat.projectiles().impacts() > 0,
            "tower could not kill boss or death was not counted");
}

void hudAndWaves(Engine& engine) {
    LevelRuntime runtime;
    runtime.load(engine, "assets/test_fixtures/levels/boss_watch.json");
    GameHud hud{engine, runtime};
    const auto render = [&] {
        require(engine.update(), "boss HUD render failed");
    };
    render();
    require(!ImGui::FindWindowByName("Boss Health"), "boss HUD visible before encounter");
    runtime.startWave();
    auto& enemies = *runtime.navigation().enemies();
    const auto type = bossType(enemies);
    require(runtime.navigation().spawnAt(engine, type, {{-6, -2}, {-6, -2}, 0}, 1) == 1 &&
                runtime.navigation().spawnAt(engine, type, {{-6, 2}, {-6, 2}, 0}, 1) == 1,
            "multiple boss spawn failed");
    const auto first = enemies.enemies().front().id, second = enemies.enemies().back().id;
    render();
    render();
    auto* panel = ImGui::FindWindowByName("Boss Health");
    require(panel && panel->Active && panel->Size.y > 120, "stacked boss bars missing");
    std::vector<EnemyId> extraBosses;
    for (const glm::dvec2 point : {glm::dvec2{-3, -3}, {-3, 3}, {0, -3}, {0, 3}}) {
        require(runtime.navigation().spawnAt(engine, type, {point, point, 0}, 1) == 1, "extra boss spawn failed");
        extraBosses.push_back(enemies.enemies().back().id);
    }
    render();
    render();
    require(panel->ScrollbarY && panel->ScrollMax.y > 0, "multiple boss bars overflowed without scrolling");
    for (const auto id : extraBosses) {
        require(enemies.remove(id), "extra boss cleanup failed");
    }
    const auto filledWidth = [&] {
        float minimum = 1e9F, maximum = -1e9F;
        for (const auto& vertex : panel->DrawList->VtxBuffer) {
            if (vertex.col == IM_COL32(180, 48, 72, 255)) {
                minimum = std::min(minimum, vertex.pos.x);
                maximum = std::max(maximum, vertex.pos.x);
            }
        }
        return maximum - minimum;
    };
    require(enemies.remove(second), "second boss cleanup failed");
    render();
    render();
    const float fullWidth = filledWidth();
    require(enemies.applyDamage(first, enemies.find(first)->maximumHealth * 0.5F), "boss HUD damage failed");
    render();
    const float changing = filledWidth();
    require(changing < fullWidth && changing > fullWidth * 0.5F, "boss health did not interpolate");
    for (int frame = 0; frame < 45; ++frame) {
        render();
    }
    require(filledWidth() < changing, "boss health interpolation stalled");
    require(enemies.applyDamage(first, enemies.find(first)->health), "boss death failed");
    render();
    require(!panel->Active, "dead boss left a visible health bar");
    runtime.restart(engine);
    // Exercise the authored elevated encounter without changing its handcrafted schedule.
    bool sawBoss = false, elevated = false;
    for (int wave = 0; wave < 2; ++wave) {
        runtime.startWave();
        for (int frame = 0; frame < 1800 && runtime.level().waves()->phase() == LevelPhase::battle; ++frame) {
            runtime.update(engine, 0.1);
            std::vector<EnemyId> dispatched;
            for (const auto& enemy : runtime.navigation().enemies()->enemies()) {
                if (runtime.navigation().enemies()->definition(enemy.type).boss) {
                    sawBoss = true;
                    elevated = elevated || enemy.position.y > 1.5;
                    require(enemy.navigation->contains(enemy.position), "boss clipped elevated terrain");
                }
                const auto& definition = runtime.level().waves()->definition();
                const glm::dvec3 objective{definition.objectivePosition.x,
                                           definition.objectiveHeight,
                                           definition.objectivePosition.y};
                if (glm::length(enemy.position - objective) < 2) {
                    dispatched.push_back(enemy.id);
                }
            }
            // Reaching the objective now starts a siege; resolve this traversal fixture with explicit kills.
            for (const auto id : dispatched) {
                const auto* enemy = runtime.navigation().enemies()->find(id);
                require(enemy && runtime.navigation().enemies()->applyDamage(id, enemy->health),
                        "Elevated encounter defender failed to dispatch an attacker");
            }
        }
        if (runtime.level().waves()->phase() != (wave ? LevelPhase::victory : LevelPhase::preparation)) {
            std::cout << "Boss scenario wave " << wave << ": " << levelPhaseName(runtime.level().waves()->phase())
                      << "; pending " << runtime.level().waves()->pending() << '\n';
            for (const auto& enemy : runtime.navigation().enemies()->enemies()) {
                std::cout << runtime.navigation().enemies()->definition(enemy.type).name << " at " << enemy.position.x
                          << ',' << enemy.position.y << ',' << enemy.position.z << "; state "
                          << enemyStateName(enemy.state) << "; target " << static_cast<int>(enemy.combat.target.kind)
                          << "; health " << enemy.health << '\n';
            }
        }
        require(runtime.level().waves()->phase() == (wave ? LevelPhase::victory : LevelPhase::preparation),
                "boss wave failed to resolve");
    }
    require(sawBoss && elevated && runtime.level().economy().gold() == 230, "boss wave integration/reward failed");
    render();
    require(!panel->Active, "ended encounter retained boss HUD");

    auto definition = runtime.level().waves()->definition();
    definition.waves = {definition.waves.back()};
    definition.heroSpawns.clear();
    for (auto& group : definition.waves.front().groups) {
        group.count = group.batchSize = 1;
        group.start = 0;
    }
    runtime.heroes()->clear();
    runtime.level().begin(engine, runtime.navigation(), definition);
    runtime.startWave();
    runtime.update(engine, 0);
    auto& encounter = *runtime.navigation().enemies();
    EnemyId minion = 0, boss = 0;
    for (const auto& enemy : encounter.enemies()) {
        (encounter.definition(enemy.type).boss ? boss : minion) = enemy.id;
    }
    require(minion && boss && encounter.applyDamage(minion, encounter.find(minion)->health),
            "boss/minion wave fixture failed");
    runtime.update(engine, 0);
    require(runtime.level().waves()->phase() == LevelPhase::battle && runtime.level().waves()->pending() == 0,
            "wave ended while boss remained alive");
    render();
    render();
    require(panel->Active, "new boss encounter did not restore HUD");
    require(encounter.applyDamage(boss, encounter.find(boss)->health), "last boss damage failed");
    runtime.update(engine, 0);
    render();
    require(runtime.level().waves()->phase() == LevelPhase::victory && !panel->Active &&
                encounter.metrics().defeated == 2,
            "boss death did not complete wave and remove HUD");
}
} // namespace

int main() {
    try {
        Engine engine{EngineConfig{.windowTitle = "Boss runtime checks"}};
        combat(engine);
        hudAndWaves(engine);
        std::cout << "PASS boss combat, clearance, elevated waves, multiple HUD bars, smoothing and death cleanup.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL boss runtime: " << error.what() << '\n';
        return 1;
    }
}
