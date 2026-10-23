#include "navigation/navigation_session.hpp"
#include "systems/enemy_combat_system.hpp"
#include "systems/health.hpp"
#include "systems/hero_combat_system.hpp"

#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace grimlock;
using namespace grimlock::game;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

EnemyId spawn(Engine& engine, NavigationSession& navigation, glm::dvec2 position, EnemyTypeId type = 0) {
    require(navigation.spawnAt(engine, type, {position, position, 0}, 1) == 1, "battle spawn failed");
    return navigation.enemies()->enemies().back().id;
}

void heroNavigation(Engine& engine) {
    auto scene = std::make_unique<Scene>("Battle melee geometry regression");
    scene->setNavigationGrid(GridConfiguration{24, 20, 1, {-12, 0, -10}});
    for (std::uint32_t row = 0; row < 13; ++row) {
        scene->navigationGrid()->setWalkable({12, row}, false);
    }
    engine.setScene(std::move(scene));
    NavigationSession navigation;
    navigation.synchronize(engine);
    HeroSystem heroes{engine};
    heroes.synchronize(navigation.towers());
    HeroCombatSystem combat;
    const auto hero = heroes.deploy({-0.65, 0, -2});
    const auto tiger = spawn(engine, navigation, {-3, -2}, 10);
    require(!navigation.enemies()->find(tiger)->navigation->contains(heroes.find(hero)->position),
            "profile mismatch regression fixture no longer exercises distinct agent radii");
    combat.update(heroes, *navigation.enemies(), 0.4, true);
    require(navigation.enemies()->find(tiger)->health == 75,
            "hero melee incorrectly required standing on the target's larger-radius NavMesh");
    navigation.enemies()->clear();
    (void)heroes.deploy({-3, 0, 2}, hero);
    const auto acrossCorner = spawn(engine, navigation, {2, 3});
    require(combat.attack(heroes, *navigation.enemies(), hero, acrossCorner), "corner attack command rejected");
    for (int frame = 0; frame < 1200 && navigation.enemies()->find(acrossCorner); ++frame) {
        combat.update(heroes, *navigation.enemies(), 1.0 / 60, true);
    }
    require(!navigation.enemies()->find(acrossCorner), "hero failed to find a reachable approach around wall end");
    for (int frame = 0; frame < 1200; ++frame) {
        combat.update(heroes, *navigation.enemies(), 1.0 / 60, true);
    }
    require(glm::length(heroes.find(hero)->position - heroes.find(hero)->defensivePosition) < 0.2,
            "hero did not return to its assigned defensive position");

    for (std::uint32_t row = 13; row < 20; ++row) {
        engine.scene()->navigationGrid()->setWalkable({12, row}, false);
    }
    navigation.synchronize(engine);
    heroes.synchronize(navigation.towers());
    (void)heroes.deploy({-0.65, 0, -2}, hero);
    const auto inaccessible = spawn(engine, navigation, {1.65, -2});
    const auto reachable = spawn(engine, navigation, {-4, -2});
    for (int frame = 0; frame < 600 && navigation.enemies()->find(reachable); ++frame) {
        combat.update(heroes, *navigation.enemies(), 1.0 / 60, true);
    }
    require(!navigation.enemies()->find(reachable) && navigation.enemies()->find(inaccessible)->health == 100,
            "unreachable nearest enemy starved a reachable target or hero hit through a wall");
    navigation.enemies()->clear();
    require(heroes.command(hero, {-7, 0, 0}), "manual move failed");
    const auto ignored = spawn(engine, navigation, {-4, 0});
    for (int frame = 0; frame < 500 && heroes.find(hero)->state == HeroState::moving; ++frame) {
        combat.update(heroes, *navigation.enemies(), 1.0 / 60, true);
    }
    require(heroes.find(hero)->state == HeroState::reached && !heroes.find(hero)->combat.target,
            "manual movement lost priority to automatic combat");
    for (int frame = 0; frame < 600 && navigation.enemies()->find(ignored); ++frame) {
        combat.update(heroes, *navigation.enemies(), 1.0 / 60, true);
    }
    require(!navigation.enemies()->find(ignored), "hero failed to resume autonomous combat after manual movement");
}

void enemyBattle(Engine& engine) {
    auto scene = std::make_unique<Scene>("Autonomous battle fixture");
    scene->setNavigationGrid(GridConfiguration{30, 24, 1, {-15, 0, -12}});
    engine.setScene(std::move(scene));
    NavigationSession navigation;
    navigation.synchronize(engine);
    navigation.setDestination(glm::dvec2{12, 0});
    HeroSystem heroes{engine};
    heroes.synchronize(navigation.towers());
    HeroCombatSystem heroCombat;
    EnemyCombatSystem enemyCombat;
    auto& enemies = *navigation.enemies();
    const auto tick = [&](double dt, bool automaticHeroes) {
        enemyCombat.prepare(enemies, navigation.towers(), &heroes, {12, 0}, dt);
        navigation.update(engine, dt);
        enemyCombat.resolve(engine, enemies, navigation.towers(), &heroes, dt);
        navigation.synchronize(engine);
        heroes.synchronize(navigation.towers());
        if (automaticHeroes) {
            heroCombat.update(heroes, enemies, dt, true);
        } else {
            heroes.update(dt, true, enemies.enemies());
        }
    };
    const auto hero = heroes.deploy({-4, 0, 0});
    require(heroes.applyDamage(hero, 92), "hero health fixture failed");
    auto decoy = navigation.towers().definition("basic").instance();
    decoy.position = {-5, decoy.halfExtent.y, 2};
    const auto decoyId = navigation.towers().create(engine, decoy);
    navigation.synchronize(engine);
    heroes.synchronize(navigation.towers());
    const auto attacker = spawn(engine, navigation, {-6, 0});
    tick(1.0 / 60, false);
    require(enemies.find(attacker)->combat.target == DefensiveTarget{DefensiveTargetKind::hero, hero},
            "configured hero priority lost to a nearby tower");
    require(navigation.removeTower(engine, decoyId), "priority decoy cleanup failed");
    for (int frame = 0; frame < 600 && heroes.find(hero)->health > 0; ++frame) {
        tick(1.0 / 60, false);
    }
    require(heroes.find(hero)->health == 0 && !heroes.find(hero)->controllable && !heroes.command(hero, {-8, 0, 0}) &&
                !heroCombat.activate(heroes, hero, HeroAbility::cleave, true),
            "enemy failed to approach/kill hero, or dead hero accepted actions");
    const auto deadPosition = heroes.find(hero)->position;
    for (int frame = 0; frame < 120; ++frame) {
        tick(1.0 / 60, true);
    }
    require(heroes.find(hero)->position == deadPosition && enemies.find(attacker) &&
                enemies.find(attacker)->position.x > -4,
            "dead hero blocked movement or enemy failed to resume objective");
    enemies.clear();

    auto tower = navigation.towers().definition("basic").instance();
    tower.position = {1, tower.halfExtent.y, 0};
    tower.rotation = glm::angleAxis(0.4, glm::dvec3{0, 1, 0});
    const auto towerId = navigation.towers().create(engine, tower);
    navigation.synchronize(engine);
    heroes.synchronize(navigation.towers());
    require(!navigation.navigation().contains({1, 0, 0}), "tower navigation obstacle missing");
    const auto revision = navigation.navigation().revision();
    require(navigation.towers().applyDamage(engine, towerId, 242), "tower damage setup failed");
    const auto siege = spawn(engine, navigation, {-2, 0});
    for (int frame = 0; frame < 900 && navigation.towers().contains(towerId); ++frame) {
        tick(1.0 / 60, false);
    }
    require(!navigation.towers().contains(towerId) && !engine.scene()->contains(towerId) &&
                navigation.navigation().revision() != revision && navigation.navigation().contains({1, 0, 0}),
            "enemy failed to destroy tower or destroyed footprint did not restore navigation");
    for (int frame = 0; frame < 1200 && enemies.find(siege); ++frame) {
        tick(1.0 / 60, false);
    }
    require(!enemies.find(siege) && enemies.metrics().arrived > 0,
            "enemy failed to continue to objective after tower destruction");

    enemies.clear();
    auto large = navigation.towers().definition("wide").instance();
    large.position = {0, large.halfExtent.y, 0};
    const auto largeId = navigation.towers().create(engine, large);
    navigation.synchronize(engine);
    require(navigation.spawnAt(engine, 0, {{-3, -3}, {3, 3}, 0}, 40) >= 20, "melee group fixture failed");
    std::size_t simultaneous = 0;
    const auto begin = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 1200 && navigation.towers().contains(largeId); ++frame) {
        tick(1.0 / 60, false);
        std::size_t attacking = 0;
        for (const auto& enemy : enemies.enemies()) {
            if (enemy.combat.target == DefensiveTarget{DefensiveTargetKind::tower, largeId} && enemy.combat.swinging) {
                ++attacking;
            }
            require(enemy.navigation->contains(enemy.position), "combat crowd left NavMesh");
        }
        simultaneous = std::max(simultaneous, attacking);
    }
    if (navigation.towers().contains(largeId)) {
        std::cout << "Siege remaining health: " << navigation.towers().progress(largeId).health
                  << "; simultaneous attackers: " << simultaneous << '\n';
    }
    require(simultaneous >= 3 && !navigation.towers().contains(largeId), "melee crowd deadlocked around tower");
    std::cout << "Melee group: " << simultaneous << " simultaneous attackers; simulation "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count()
              << " ms.\n";
    enemies.clear();

    const auto defender = heroes.deploy({-4, 0, 5});
    const auto opponent = spawn(engine, navigation, {-6, 5});
    for (int frame = 0; frame < 900 && enemies.find(opponent); ++frame) {
        tick(1.0 / 60, true);
    }
    require(!enemies.find(opponent) && heroes.find(defender)->health > 0 && heroes.find(defender)->health < 100,
            "autonomous hero/enemy duel did not exchange damage and resolve");
    require(heroes.applyDamage(defender, 100), "large battle defender setup failed");
    enemies.clear();
    const auto firstDefender = heroes.deploy({-5, 0, -5});
    const auto secondDefender = heroes.deploy({4, 0, -5});
    const auto crowdTower = navigation.towers().create(engine, large);
    require(navigation.towers().contains(crowdTower), "large combat tower setup failed");
    navigation.synchronize(engine);
    require(navigation.spawnAt(engine, 1, {{-9, -9}, {-6, -6}, 0}, 8) == 8, "mixed combat group spawn failed");
    require(navigation.spawnAt(engine, 0, {{-12, -10}, {10, 10}, 0}, 500) == 500, "large combat group spawn failed");
    const auto crowdStart = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 120; ++frame) {
        tick(1.0 / 60, true);
    }
    for (const auto& enemy : enemies.enemies()) {
        require(enemy.navigation->contains(enemy.position), "large combat group left navigation");
        require(enemy.combat.target != DefensiveTarget{DefensiveTargetKind::hero, hero} &&
                    enemy.combat.target != DefensiveTarget{DefensiveTargetKind::hero, defender},
                "large combat group targeted a dead hero");
    }
    require(heroes.find(firstDefender)->health < 100 && heroes.find(secondDefender)->health < 100,
            "both heroes did not participate in the mixed autonomous battle");
    std::cout << "508 mixed combat agents with two heroes: "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - crowdStart).count() / 120
              << " ms average simulation tick.\n";
    require(engine.update(), "autonomous combat render failed");
    enemyCombat.stop(enemies);
}

void unreachableDefender(Engine& engine) {
    auto scene = std::make_unique<Scene>("Unreachable defender regression");
    scene->setNavigationGrid(GridConfiguration{24, 16, 1, {-12, 0, -8}});
    for (std::uint32_t row = 0; row < 16; ++row) {
        scene->navigationGrid()->setWalkable({12, row}, false);
    }
    engine.setScene(std::move(scene));
    NavigationSession navigation;
    navigation.synchronize(engine);
    navigation.setDestination(glm::dvec2{-10, 0});
    HeroSystem heroes{engine};
    heroes.synchronize(navigation.towers());
    const auto hero = heroes.deploy({1.65, 0, 0});
    const auto enemy = spawn(engine, navigation, {-0.65, 0});
    EnemyCombatSystem combat;
    for (int frame = 0; frame < 300; ++frame) {
        combat.prepare(*navigation.enemies(), navigation.towers(), &heroes, {-10, 0}, 1.0 / 60);
        navigation.update(engine, 1.0 / 60);
        combat.resolve(engine, *navigation.enemies(), navigation.towers(), &heroes, 1.0 / 60);
    }
    const auto* remaining = navigation.enemies()->find(enemy);
    require(
        heroes.find(hero)->health == 100 &&
            (!remaining || (remaining->combat.target.kind != DefensiveTargetKind::hero && remaining->position.x < -2)),
        "enemy hit through a wall or stalled instead of continuing to objective");
}
} // namespace

int main() {
    try {
        float health = 10;
        require(receiveDamage(health, 20.0F) && health == 0, "shared health did not saturate");
        bool rejected = false;
        try {
            receiveDamage(health, std::numeric_limits<float>::quiet_NaN());
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "shared health accepted NaN");
        Engine engine{EngineConfig{.windowTitle = "Autonomous battle checks"}};
        heroNavigation(engine);
        enemyBattle(engine);
        unreachableDefender(engine);
        std::cout
            << "PASS autonomous heroes, reachable perimeter attacks, profile/radius regression, return to defense, "
               "manual priority/recovery, enemy siege/hero death/objective, crowd engagement and shared damage.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Battle: " << error.what() << '\n';
        return 1;
    }
}
