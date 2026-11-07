#include "navigation/navigation_session.hpp"
#include "runtime/level_runtime.hpp"
#include "systems/combat_system.hpp"
#include "systems/enemy_combat_system.hpp"
#include "systems/hero_combat_system.hpp"
#include "systems/wave_system.hpp"

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

EnemyTypeId typeOf(NavigationSession& navigation, const char* name) {
    for (EnemyTypeId type = 0; type < navigation.enemies()->typeCount(); ++type) {
        if (navigation.enemies()->definition(type).name == name) {
            return type;
        }
    }
    throw std::runtime_error("Undead definition missing");
}

void flatScene(Engine& engine) {
    auto scene = std::make_unique<Scene>("Undead lifecycle");
    scene->setNavigationGrid(GridConfiguration{24, 16, 1, {-12, 0, -8}});
    engine.setScene(std::move(scene));
}

void skeletonCombat(Engine& engine) {
    flatScene(engine);
    NavigationSession navigation;
    navigation.synchronize(engine);
    const auto type = typeOf(navigation, "Skeleton");
    HeroSystem heroes{engine};
    heroes.synchronize(navigation.towers());
    const auto hero = heroes.deploy({-0.8, 0, 0});
    auto tower = navigation.towers().definition("basic").instance();
    tower.position = {1, tower.halfExtent.y, 0};
    const auto object = navigation.towers().create(engine, tower);
    navigation.synchronize(engine);
    navigation.setDestination(glm::dvec2{8, 0});
    require(navigation.spawnAt(engine, type, {{0, 0}, {0, 0}, 0}, 1) == 1, "Skeleton combat spawn failed");
    auto& enemies = *navigation.enemies();
    EnemyCombatSystem combat;
    combat.prepare(enemies, navigation.towers(), &heroes, {8, 0}, 0.01, &navigation.combatGeometry());
    require(enemies.enemies().front().combat.target.kind == DefensiveTargetKind::tower,
            "Skeleton did not retain the first detected defender");
    for (int frame = 0; frame < 180; ++frame) {
        combat.prepare(enemies, navigation.towers(), &heroes, {8, 0}, 1.0 / 120, &navigation.combatGeometry());
        navigation.update(engine, 1.0 / 120);
        combat.resolve(engine, enemies, navigation.towers(), &heroes, 1.0 / 120);
    }
    const auto& health = navigation.towers().progress(object);
    require(health.health < health.maximumHealth && heroes.find(hero)->health == heroes.find(hero)->maximumHealth,
            "Skeleton melee failed or switched to a different defender");
}

void resurrection(Engine& engine) {
    flatScene(engine);
    NavigationSession navigation;
    navigation.synchronize(engine);
    const auto type = typeOf(navigation, "Skeleton");
    auto& enemies = *navigation.enemies();
    require(enemies.definition(type).maximumHealth < enemies.definition(typeOf(navigation, "Goblin")).maximumHealth &&
                enemies.definition(type).combat.firstDetected,
            "Skeleton statistics or targeting incorrect");
    navigation.setDestination(glm::dvec2{8, 0});
    auto level = loadLevelDefinition(engine.assetRoot() / "test_fixtures/levels/first_watch.json");
    level.waves = {{"Skeleton resurrection", {{"Skeleton", 0, 2, 2, 0, 1}}, 0}};
    WaveSystem waves{level};
    waves.startWave();
    waves.update(0, 0, 0, [&](const WaveGroup&, std::size_t count) {
        return navigation.spawnAt(engine, type, {{0, 0}, {1, 0}, 0}, count);
    });
    require(enemies.enemies().size() == 2, "Skeleton wave spawn failed");
    const auto first = enemies.enemies()[0].id, second = enemies.enemies()[1].id;
    const auto position = enemies.find(first)->position;
    const auto instance = *enemies.find(first)->instance;
    for (int cycle = 0; cycle < 3; ++cycle) {
        require(enemies.applyDamage(first, 100), "Skeleton lethal hit failed");
        require(enemies.find(first) && !enemies.find(first)->alive && enemies.metrics().defeated == 0 &&
                    enemies.coinEffectCount() == 0 && enemies.enemies().size() == 2,
                "Resurrectable skeleton was removed or rewarded");
        require(!enemies.applyDamage(first, 100), "Dying skeleton remained targetable");
        const auto& playback = engine.animation(instance);
        require(playback.model().animations[playback.clip()].name == "die" && !playback.looping(),
                "Skeleton did not play actual death clip once");
        enemies.updateEffects(5);
        require(enemies.find(first)->life == Enemy::Life::dying, "Death animation was interrupted");
        engine.animation(instance).update(5);
        enemies.updateEffects(0);
        require(enemies.find(first)->life == Enemy::Life::bones && enemies.find(first)->health == 5,
                "Destructible bones not created");
        enemies.update(navigation.surface(engine, type), navigation.destination(), 0.1);
        require(enemies.find(first)->position == position && !enemies.find(first)->combat.swinging,
                "Bones moved or attacked");
        enemies.updateEffects(2.8);
        require(enemies.find(first)->life == Enemy::Life::bones, "Skeleton revived before three seconds");
        waves.update(0.1, enemies.enemies().size(), 0, [](const WaveGroup&, std::size_t) {
            return 0;
        });
        require(waves.phase() == LevelPhase::battle && waves.pending() == 0,
                "Wave completed with bones awaiting resurrection");
        enemies.updateEffects(0.11);
        require(enemies.find(first)->life == Enemy::Life::living && enemies.find(first)->health == 40 &&
                    *enemies.find(first)->instance == instance && enemies.find(first)->position == position,
                "Skeleton did not revive in place with its original entity/model");
    }
    require(enemies.applyDamage(first, 100) && enemies.applyDamage(second, 100), "Independent deaths failed");
    engine.animation(instance).update(5);
    engine.animation(*enemies.find(second)->instance).update(5);
    enemies.updateEffects(5);
    require(enemies.find(first)->life == Enemy::Life::bones && enemies.find(second)->life == Enemy::Life::bones,
            "Concurrent bone piles failed");
    auto tower = navigation.towers().definition("basic").instance();
    tower.position = {3, tower.halfExtent.y, 0};
    (void)navigation.towers().create(engine, tower);
    navigation.synchronize(engine);
    CombatSystem towerCombat{engine};
    towerCombat.update(navigation.towers(), enemies, 0.01, &navigation.combatGeometry());
    require(towerCombat.shotsFired() == 1, "Tower could not target destructible bones");
    HeroSystem heroes{engine};
    heroes.synchronize(navigation.towers());
    const auto knight = heroes.deploy({-1, 0, 0});
    HeroCombatSystem heroCombat;
    require(heroCombat.attack(heroes, enemies, knight, first), "Hero could not target destructible bones");
    require(enemies.applyDamage(first, 5) && !enemies.find(first) && enemies.find(second),
            "Destroying bones did not kill only their owner");
    require(enemies.metrics().defeated == 1 && enemies.coinEffectCount() == 1, "Permanent death reward repeated");
    enemies.updateEffects(3.1);
    require(enemies.find(second)->life == Enemy::Life::living && !enemies.find(first), "Destroyed bones revived");
    enemies.clear();
    require(enemies.enemies().empty() && enemies.coinEffectCount() == 0 && enemies.deathEffectCount() == 0,
            "Undead cleanup leaked instances");
}

void ghostCombat(Engine& engine) {
    flatScene(engine);
    NavigationSession navigation;
    navigation.synchronize(engine);
    const auto ghostType = typeOf(navigation, "Ghost");
    auto& enemies = *navigation.enemies();
    require(navigation.spawnAt(engine, ghostType, {{0, 0}, {1, 0}, 0}, 2) == 2, "Ghost spawn failed");
    const auto first = enemies.enemies()[0].id, second = enemies.enemies()[1].id;
    require(!enemies.applyDamage(first, 100) && enemies.find(first)->health == 30, "Ghost accepted physical damage");
    HeroSystem heroes{engine};
    heroes.synchronize(navigation.towers());
    std::vector<HeroId> physical;
    HeroId mage = 0;
    for (std::size_t type = 0; type < heroes.typeCount(); ++type) {
        const auto id = heroes.deploy({-2.0 - type, 0, 0}, {}, type);
        if (heroes.definition(type).id == "mage") {
            mage = id;
        } else {
            physical.push_back(id);
        }
    }
    HeroCombatSystem heroCombat;
    for (const auto hero : physical) {
        require(!heroCombat.attack(heroes, enemies, hero, first), "Physical hero accepted immune target");
    }
    require(mage && heroCombat.attack(heroes, enemies, mage, first), "Mage could not target Ghost");
    heroCombat.update(heroes, enemies, 0.01, true);
    for (const auto hero : physical) {
        require(!heroes.find(hero)->combat.target, "Physical hero automatically targeted Ghost");
    }
    auto tower = navigation.towers().definition("basic").instance();
    tower.position = {2, tower.halfExtent.y, 0};
    (void)navigation.towers().create(engine, tower);
    navigation.synchronize(engine);
    CombatSystem towers{engine};
    towers.update(navigation.towers(), enemies, 0.5, &navigation.combatGeometry());
    require(towers.shotsFired() == 0, "Physical tower targeted immune Ghost");
    heroCombat.update(heroes, enemies, 1, true);
    require(enemies.find(first) && enemies.find(first)->health == 10 && enemies.find(second)->health == 10,
            "Mage basic attack did not apply magic splash to Ghosts");
    // Exercise the same magic projectile and splash path used by the Mage's basic attack.
    ProjectileSystem projectiles{engine};
    ProjectileDefinition magic;
    magic.damage = 30;
    magic.damageType = DamageType::magic;
    magic.splashRadius = 1.5;
    projectiles.spawn({0, 1, -1}, first, magic);
    projectiles.update(enemies, 1, &navigation.combatGeometry());
    require(!enemies.find(first) && !enemies.find(second) && enemies.metrics().defeated == 2,
            "Magic splash did not kill Ghosts");
}

void mausoleumSiege(Engine& engine) {
    LevelRuntime runtime;
    runtime.load(engine, "assets/levels/forest_defense.json");
    for (const auto* name : {"Goblin", "Skeleton", "Ghost"}) {
        auto& navigation = runtime.navigation();
        auto& enemies = *navigation.enemies();
        runtime.heroes()->clear();
        const auto type = typeOf(navigation, name);
        const auto original = engine.scene()->object(105).local;
        require(engine.scene()->object(105).model == "environment/graveyard/crypt.glb", "Mausoleum model missing");
        require(navigation.spawnAt(engine, type, {{4, 12}, {4, 12}, 2}, 1) == 1, "Mausoleum attacker could not spawn");
        const auto id = enemies.enemies().front().id;
        runtime.startWave();
        for (int frame = 0; frame < 180; ++frame) {
            runtime.update(engine, 1.0 / 60);
        }
        require(enemies.find(id) && enemies.find(id)->alive && enemies.metrics().arrived == 0 &&
                    runtime.level().waves()->health() < 9 && runtime.level().waves()->health() > 0 &&
                    runtime.level().waves()->phase() == LevelPhase::battle,
                "Enemy disappeared on arrival or did not repeatedly attack the mausoleum");
        for (int frame = 0; frame < 1200 && runtime.level().waves()->phase() == LevelPhase::battle; ++frame) {
            runtime.update(engine, 1.0 / 60);
        }
        require(runtime.level().waves()->phase() == LevelPhase::defeat && runtime.level().waves()->health() == 0 &&
                    enemies.find(id) && engine.scene()->object(105).local.scale.y < original.scale.y,
                "Mausoleum did not break or removed its attacker");
        require(engine.update(), "Broken mausoleum render failed");
        runtime.restart(engine);
        require(runtime.level().waves()->health() == 10 && navigation.enemies()->enemies().empty() &&
                    engine.scene()->object(105).local.scale == original.scale,
                "Restart did not restore the mausoleum");
    }
    runtime.unload(engine);
}

void ghostRoutes(Engine& engine) {
    flatScene(engine);
    NavigationSession navigation;
    navigation.synchronize(engine);
    auto tower = navigation.towers().definition("basic").instance();
    tower.position = {0, tower.halfExtent.y, 0};
    (void)navigation.towers().create(engine, tower);
    navigation.synchronize(engine);
    const auto type = typeOf(navigation, "Ghost");
    const auto& ground = navigation.surface(engine, type);
    require(ground.contains({0, 0, 0}) && !navigation.surface(engine, typeOf(navigation, "Goblin")).contains({0, 0, 0}),
            "Ghost did not ignore structure obstruction independently of normal navigation");
    navigation.setDestination(glm::dvec2{4, 0});
    for (int ghost = 0; ghost < 8; ++ghost) {
        require(navigation.spawnAt(engine, type, {{-4, 0}, {-4, 0}, 0}, 1) == 1, "Overlapping Ghost spawn failed");
    }
    bool crossed = false;
    EnemyCombatSystem combat;
    for (int frame = 0; frame < 600 && !navigation.enemies()->enemies().empty(); ++frame) {
        combat.prepare(*navigation.enemies(),
                       navigation.towers(),
                       nullptr,
                       {4, 0},
                       1.0 / 60,
                       &navigation.combatGeometry());
        navigation.update(engine, 1.0 / 60);
        for (const auto& ghost : navigation.enemies()->enemies()) {
            require(ground.contains(ghost.position) && ghost.hoverOffset >= 0.15 &&
                        ghost.combat.target.kind == DefensiveTargetKind::none,
                    "Ghost left ground, stopped hovering or attacked a defender");
            crossed = crossed || std::abs(ghost.position.x) < 0.2;
        }
    }
    require(crossed && navigation.enemies()->metrics().arrived == 8, "Ghost failed to pass tower and reach objective");

    // Imported terrain contains real voids: terrain-only navigation must retain those gaps.
    auto scene = std::make_unique<Scene>("Ghost void check");
    for (const double x : {-4.0, 4.0}) {
        const auto object = scene->createObject("Island");
        scene->setModel(object, "towers/structures/primitives/cube.glb");
        scene->setRole(object, "grimlock.terrain");
        Transform transform;
        transform.position = {x, -0.5, 0};
        transform.scale = {2, 0.5, 3};
        scene->setTransform(object, transform);
    }
    engine.setScene(std::move(scene));
    navigation.synchronize(engine);
    const auto& islands = navigation.surface(engine, type);
    NavigationPath path;
    require(!islands.contains({0, 0, 0}) && !islands.findPath({-4, 0, 0}, {4, 0, 0}, path),
            "Ghost navigation crossed a terrain void");

    engine.setScene(engine.loadScene(engine.assetRoot() / "scenes/hill_watch.json"));
    navigation.synchronize(engine);
    const auto& elevated = navigation.surface(engine, type);
    navigation.setDestination(glm::dvec2{10, 0});
    navigation.setDestinationHeight(2);
    require(navigation.spawnAt(engine, type, {{-7, -1}, {-7, -1}, 0}, 1) == 1, "Elevated Ghost spawn failed");
    bool climbed = false;
    for (int frame = 0; frame < 1500 && !navigation.enemies()->enemies().empty(); ++frame) {
        navigation.update(engine, 1.0 / 60);
        for (const auto& ghost : navigation.enemies()->enemies()) {
            require(elevated.contains(ghost.position), "Hovering Ghost left elevated walkable terrain");
            climbed = climbed || ghost.position.y > 1.8;
        }
    }
    require(climbed && navigation.enemies()->metrics().arrived == 1,
            "Ghost did not follow elevated terrain to the objective");
}
} // namespace

int main() {
    try {
        Engine engine{EngineConfig{.windowTitle = "Skeleton and Ghost checks"}};
        skeletonCombat(engine);
        resurrection(engine);
        ghostCombat(engine);
        ghostRoutes(engine);
        mausoleumSiege(engine);
        require(engine.update(), "Undead render failed");
        std::cout << "PASS Skeleton resurrection, destructible bones, Ghost routing and magic immunity.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Undead: " << error.what() << '\n';
        return 1;
    }
}
