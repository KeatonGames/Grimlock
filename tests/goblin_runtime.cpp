#include "navigation/navigation_session.hpp"
#include "runtime/level_runtime.hpp"
#include "systems/enemy_combat_system.hpp"

#include <algorithm>
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

EnemyDefinition goblinDefinition(Engine& engine) {
    for (const auto& definition : loadAnimalDefinitions(engine.assetRoot())) {
        if (definition.name == "Goblin") {
            return definition;
        }
    }
    throw std::runtime_error("Goblin definition missing");
}

EnemyTypeId goblinType(NavigationSession& navigation) {
    const auto& enemies = *navigation.enemies();
    for (EnemyTypeId type = 0; type < enemies.typeCount(); ++type) {
        if (enemies.definition(type).name == "Goblin") {
            return type;
        }
    }
    throw std::runtime_error("Goblin navigation profile missing");
}

void behavior(Engine& engine) {
    auto scene = std::make_unique<Scene>("Goblin first detection and lifecycle");
    scene->setNavigationGrid(GridConfiguration{24, 16, 1, {-12, 0, -8}});
    engine.setScene(std::move(scene));
    NavigationSession navigation;
    navigation.synchronize(engine);
    auto definition = goblinDefinition(engine);
    require(definition.matchesName("Grunt") && definition.combat.firstDetected && definition.maximumHealth == 60,
            "Goblin compatibility or basic statistics missing");
    definition.combat.heroPriority = 100;
    definition.combat.towerPriority = 0;
    EnemySystem enemies{engine, definition};
    HeroSystem heroes{engine};
    heroes.synchronize(navigation.towers());
    const auto hero = heroes.deploy({-0.8, 0, 0});
    auto tower = navigation.towers().definition("basic").instance();
    tower.position = {1, tower.halfExtent.y, 0};
    const auto towerId = navigation.towers().create(engine, tower);
    navigation.synchronize(engine);
    heroes.synchronize(navigation.towers());
    const auto& mesh = navigation.surface(engine, goblinType(navigation));
    EnemyCombatSystem combat;
    const auto spawn = [&] {
        require(enemies.spawn(mesh, {{0, 0}, {0, 0}, 0}, 1) == 1, "Goblin spawn failed");
        return enemies.enemies().back().id;
    };
    const auto prepare = [&](double delta) {
        combat.prepare(enemies, navigation.towers(), &heroes, {8, 0}, delta, &navigation.combatGeometry());
    };
    const auto tick = [&](double duration) {
        while (duration > 1e-10) {
            const auto delta = std::min(duration, 1.0 / 120);
            prepare(delta);
            enemies.update(mesh, enemies.sharedDestination(), delta);
            combat.resolve(engine, enemies, navigation.towers(), &heroes, delta);
            duration -= delta;
        }
    };
    const auto cover = engine.scene()->createObject("Goblin perception cover");
    engine.scene()->setModel(cover, "towers/structures/primitives/cube.glb");
    Transform coverTransform;
    coverTransform.position = {0.5F, 1, 0};
    coverTransform.scale = {0.08F, 1, 0.35F};
    engine.scene()->setTransform(cover, coverTransform);
    engine.prepareScene(*engine.scene());
    navigation.synchronize(engine);
    const auto occluded = spawn();
    prepare(0);
    require(enemies.find(occluded)->combat.target == DefensiveTarget{DefensiveTargetKind::hero, hero},
            "Goblin detected a tower through solid cover");
    engine.scene()->removeObject(cover);
    navigation.synchronize(engine);
    prepare(0.4);
    require(enemies.find(occluded)->combat.target == DefensiveTarget{DefensiveTargetKind::hero, hero},
            "Goblin abandoned its first target when another defender became visible");
    enemies.clear();
    const auto id = spawn();
    const auto instance = *enemies.find(id)->instance;
    auto& playback = engine.animation(instance);
    const auto clipName = [&] {
        return playback.model().animations[playback.clip()].name;
    };
    require(playback.model().meshes.size() == 2 && playback.model().skins.size() == 2 &&
                playback.model().skins[0].joints.size() == 7 && clipName() == "idle" && playback.looping(),
            "Goblin material/skin/idle import changed");
    require(enemies.find(id)->radius == 0.23 && enemies.find(id)->height == 0.9,
            "Goblin navigation dimensions were derived from its outstretched arms");
    prepare(0);
    const DefensiveTarget first{DefensiveTargetKind::tower, navigation.towers().entityForObject(towerId)};
    require(enemies.find(id)->combat.target == first && clipName() == "attack-melee-right" && !playback.looping(),
            "Goblin ranked target types instead of attacking the first detected defender");
    playback.seek(0.1F);
    const auto health = navigation.towers().progress(towerId).health;
    tick(0.1);
    require(navigation.towers().progress(towerId).health == health && playback.time() == 0.1F,
            "Goblin hit before windup or restarted its attack each frame");
    require(enemies.applyDamage(id, 1, {DefensiveTargetKind::hero, hero}), "Goblin hit setup failed");
    require(enemies.find(id)->combat.target == first && enemies.find(id)->hitFlashRemaining > 0 &&
                clipName() == "attack-melee-right",
            "Hit reaction changed target or interrupted the basic swing");
    tick(0.2);
    require(navigation.towers().progress(towerId).health == health - definition.combat.damage,
            "Goblin attack missed or applied duplicate damage");
    tick(0.4);
    require(navigation.towers().progress(towerId).health == health - definition.combat.damage,
            "Goblin applied a second impact during recovery");
    require(navigation.towers().remove(engine, towerId), "Goblin tower cleanup failed");
    navigation.synchronize(engine);
    tick(0.5);
    require(enemies.find(id)->combat.target == DefensiveTarget{DefensiveTargetKind::hero, hero},
            "Goblin did not retarget after its first defender died");
    const auto heroHealth = heroes.find(hero)->health;
    for (int step = 0; step < 600 && heroes.find(hero)->health == heroHealth; ++step) {
        tick(1.0 / 120);
    }
    require(heroes.find(hero)->health == heroHealth - definition.combat.damage,
            "Goblin failed to damage its hero target exactly once");
    (void)heroes.deploy({-7, 0, 0}, hero);
    prepare(0.01);
    require(enemies.find(id)->combat.target.kind == DefensiveTargetKind::none &&
                enemies.find(id)->destination == std::optional{glm::dvec2{8, 0}},
            "Goblin chased beyond its pursuit area instead of resuming the objective");
    enemies.clear();
    heroes.clear();
    const auto walking = spawn();
    const auto walkingInstance = *enemies.find(walking)->instance;
    auto& motion = engine.animation(walkingInstance);
    require(enemies.setDestination(walking, glm::dvec2{5, 0}), "Goblin movement setup failed");
    enemies.update(mesh, {}, 0.01);
    require(motion.model().animations[motion.clip()].name == "walk", "Goblin walk did not start");
    for (int step = 0; step < 120; ++step) {
        enemies.update(mesh, {}, 1.0 / 120);
    }
    require(motion.model().animations[motion.clip()].name == "sprint" && motion.looping(), "Goblin run did not loop");
    require(enemies.setDestination(walking, {}), "Goblin stop failed");
    enemies.update(mesh, {}, 0.01);
    require(motion.model().animations[motion.clip()].name == "idle" && motion.looping(), "Goblin idle did not resume");
    require(enemies.applyDamage(walking, 100) && !enemies.find(walking) && enemies.deathEffectCount() == 1,
            "Dead Goblin remained in ECS targeting/navigation");
    require(motion.model().animations[motion.clip()].name == "die" && !motion.looping(), "Goblin death missing");
    require(enemies.coinEffectCount() == 1 && !enemies.applyDamage(walking, 100),
            "Death did not emit exactly one visual coin");
    enemies.updateEffects(0.3);
    require(enemies.coinEffectCount() == 1, "Coin disappeared before its rising animation");
    enemies.updateEffects(60);
    require(enemies.coinEffectCount() == 0, "Visual coin did not expire");
    require(enemies.deathEffectCount() == 1, "Goblin death removed before animation playback completed");
    motion.update(1);
    enemies.updateEffects(0);
    require(enemies.deathEffectCount() == 1, "Corpse skipped disintegration after death animation");
    enemies.updateEffects(0.3);
    require(enemies.deathEffectCount() == 1, "Corpse disintegration ended too early");
    enemies.updateEffects(0.4);
    require(enemies.deathEffectCount() == 0 && enemies.metrics().defeated == 1,
            "Goblin corpse or defeat count incorrect");
    enemies.clear();
    require(enemies.spawn(mesh, {{7.6, 0}, {7.6, 0}, 0}, 1) == 1, "Goblin objective setup failed");
    tick(0.3);
    require(enemies.enemies().empty() && enemies.metrics().arrived == 1 && enemies.metrics().defeated == 0,
            "Goblin objective windup broke arrival/wave accounting");
    require(engine.update(), "Goblin runtime render failed");
}

void terrainAndCrowds(Engine& engine) {
    engine.setScene(engine.loadScene(engine.assetRoot() / "scenes/hill_watch.json"));
    NavigationSession navigation;
    navigation.synchronize(engine);
    const auto& mesh = navigation.surface(engine, goblinType(navigation));
    auto definition = goblinDefinition(engine);
    definition.despawnOnArrival = false;
    EnemySystem enemies{engine, definition};
    enemies.setSharedDestination(glm::dvec2{10, 0});
    require(enemies.spawn(mesh, {{-7, -1}, {-5, 1}, 0}, 1) == 1, "Elevated Goblin spawn failed");
    enemies.setDestinationHeight(2);
    bool climbed = false;
    for (int frame = 0; frame < 1500; ++frame) {
        enemies.update(mesh, glm::dvec2{10, 0}, 1.0 / 60);
        const auto& enemy = enemies.enemies().front();
        require(mesh.contains(enemy.position), "Goblin left the elevated NavMesh");
        climbed = climbed || (enemy.position.y > 0.5 && enemy.position.y < 1.7);
    }
    require(climbed && enemies.enemies().front().position.y > 1.9 &&
                enemies.enemies().front().state == EnemyState::reached,
            "Goblin failed to traverse the authored ramp");
    enemies.clear();
    require(enemies.spawn(mesh, {{-9, -3}, {-3, 3}, 0}, 100) == 100, "Goblin crowd spawn failed");
    enemies.setDestinationHeight(2);
    const auto asset = engine.animation(*enemies.enemies().front().instance).asset();
    for (int frame = 0; frame < 300; ++frame) {
        enemies.update(mesh, glm::dvec2{10, 0}, 1.0 / 60);
        for (const auto& enemy : enemies.enemies()) {
            require(mesh.contains(enemy.position) && std::isfinite(glm::length(enemy.position)),
                    "Goblin crowd left navigation");
            require(engine.animation(*enemy.instance).asset() == asset, "Goblin instances duplicated model resources");
        }
    }
    require(engine.update(), "Goblin crowd render failed");
}

void coinExpiry(Engine& engine) {
    auto scene = std::make_unique<Scene>("Coin expiry render regression");
    scene->setNavigationGrid(GridConfiguration{8, 8, 1, {-4, 0, -4}});
    engine.setScene(std::move(scene));
    NavigationSession navigation;
    navigation.synchronize(engine);
    require(navigation.spawnAt(engine, 0, {{0, 0}, {0, 0}, 0}, 1) == 1, "Coin regression spawn failed");
    auto& enemies = *navigation.enemies();
    require(enemies.applyDamage(enemies.enemies().front().id, 1000), "Coin regression kill failed");
    enemies.updateEffects(1.25 - 0.000001);
    require(engine.update(), "Coin expiry produced an invalid render transform");
    enemies.updateEffects(0.01);
    require(enemies.coinEffectCount() == 0, "Expired coin retained a render instance");
}

void waves(Engine& engine) {
    LevelRuntime runtime;
    runtime.load(engine, "assets/test_fixtures/levels/rival_watch.json");
    require(runtime.level().waves()->definition().waves.front().groups.front().enemy == "Goblin",
            "Legacy Grunt wave did not resolve to Goblin");
    runtime.startWave();
    runtime.update(engine, 0.05);
    require(!runtime.navigation().enemies()->enemies().empty(), "Legacy wave did not spawn Goblins");
    const auto& first = runtime.navigation().enemies()->enemies().front();
    require(runtime.navigation().enemies()->definition(first.type).name == "Goblin",
            "Wave spawned unrelated enemy model");
    runtime.unload(engine);
}
} // namespace

int main() {
    try {
        Engine engine{EngineConfig{.windowTitle = "Goblin integration checks"}};
        behavior(engine);
        terrainAndCrowds(engine);
        waves(engine);
        coinExpiry(engine);
        std::cout
            << "PASS Goblin targeting, melee timing, clips, death lifecycle, crowds, elevation and legacy waves.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Goblin: " << error.what() << '\n';
        return 1;
    }
}
