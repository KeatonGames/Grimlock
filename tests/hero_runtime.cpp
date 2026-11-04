#include "input/ground_command.hpp"
#include "runtime/level_runtime.hpp"
#include "systems/enemy_combat_system.hpp"
#include "systems/hero_combat_system.hpp"
#include "systems/hero_system.hpp"

#include <grimlock/ray.hpp>
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

void gestures() {
    GroundCommandGesture gesture;
    InputState input;
    input.focused = true;
    input.rightPressPosition = glm::vec2{20, 30};
    auto& right = input.mouseButtons[static_cast<std::size_t>(MouseButton::right)];
    right = {true, true, false};
    require(!gesture.update(input, true), "right press issued a command before release");
    right = {false, false, true};
    require(gesture.update(input, true) == input.rightPressPosition, "short release did not issue command");
    right = {true, true, false};
    (void)gesture.update(input, true);
    input.rightDragDistance = 12;
    right = {false, false, true};
    require(!gesture.update(input, true), "drag returned to press position and issued a command");
    input.rightDragDistance = 0;
    right = {true, false, true};
    require(gesture.update(input, true) == input.rightPressPosition, "same-frame click lost");
    require(!gesture.update(input, false), "UI click issued a command");
    input.focused = false;
    require(!gesture.update(input, true), "unfocused click issued a command");
}

void combatChecks(Engine& engine) {
    auto scene = std::make_unique<Scene>("Hero combat fixture");
    scene->setNavigationGrid(GridConfiguration{20, 16, 1, {-10, 0, -8}});
    engine.setScene(std::move(scene));
    TowerSystem towers;
    towers.synchronize(engine);
    HeroSystem heroes{engine};
    heroes.synchronize(towers);
    EnemySystem enemies{engine, EnemyDefinition{.despawnOnArrival = false}};
    HeroCombatSystem combat;
    const auto spawn = [&](double x, double z) {
        require(enemies.spawn(heroes.navigation(), {{x, z}, {x, z}, 0}, 1) == 1, "hero combat spawn failed");
        return enemies.enemies().back().id;
    };
    const auto hero = heroes.deploy({0, 0, 0});
    const auto target = spawn(0, 1);
    combat.update(heroes, enemies, 1, false);
    require(enemies.find(target)->health == 100 && !heroes.find(hero)->combat.target,
            "hero attacked during preparation");
    combat.update(heroes, enemies, 0.1, true);
    require(heroes.find(hero)->combat.target == target && enemies.find(target)->health == 100,
            "hero failed to acquire target or damaged before windup");
    combat.update(heroes, enemies, 0.3, true);
    require(enemies.find(target)->health == 75, "hero impact timing/damage incorrect");
    combat.update(heroes, enemies, 0.2, true);
    require(enemies.find(target)->health == 75, "hero applied duplicate swing damage");
    require(heroes.command(hero, {-4, 0, 0}), "hero move override failed");
    combat.update(heroes, enemies, 0.4, true);
    require(!heroes.find(hero)->combat.target && heroes.find(hero)->position.x < -0.2 &&
                enemies.find(target)->health == 75,
            "automatic attack interrupted player movement");
    require(combat.attack(heroes, enemies, hero, target), "explicit attack failed");
    for (int i = 0; i < 600 && enemies.find(target); ++i) {
        combat.update(heroes, enemies, 1.0 / 60, true);
        require(heroes.navigation().contains(heroes.find(hero)->position), "pursuing hero left NavMesh");
    }
    require(!enemies.find(target) && enemies.metrics().defeated == 1, "hero failed to approach/kill target");
    combat.update(heroes, enemies, 1, true);
    require(!heroes.find(hero)->combat.target, "hero retained dead target");

    for (const int fps : {1, 5, 60}) {
        enemies.clear();
        (void)heroes.deploy({0, 0, 0}, hero);
        const auto victim = spawn(0, 1);
        for (int frame = 0; frame < fps * 2; ++frame) {
            combat.update(heroes, enemies, 1.0 / fps, true);
        }
        require(enemies.find(victim) && enemies.find(victim)->health == 25,
                "hero attack cadence changed with frame rate");
    }
    enemies.clear();
    (void)heroes.deploy({0, 0, 0}, hero);
    const auto interrupted = spawn(0, 1);
    combat.update(heroes, enemies, 0.1, true);
    require(heroes.find(hero)->combat.swinging && !heroes.find(hero)->combat.abilitySwing,
            "basic swing interruption fixture failed");
    require(combat.activate(heroes, hero, HeroAbility::cleave, true), "basic attack locked out manual Cleave");
    combat.update(heroes, enemies, 0.3, true);
    require(enemies.find(interrupted)->health == 55 && !combat.activate(heroes, hero, HeroAbility::cleave, true),
            "interrupted basic swing dealt duplicate damage or bypassed ability cooldown");
    enemies.clear();
    (void)heroes.deploy({0, 0, 0}, hero);
    const auto front = spawn(0, 1), side = spawn(0.7, 1), behind = spawn(0, -1), far = spawn(0, 4);
    require(!combat.activate(heroes, hero, HeroAbility::cleave, false), "Cleave allowed outside battle");
    require(combat.activate(heroes, hero, HeroAbility::cleave, true), "Cleave activation failed");
    require(!combat.activate(heroes, hero, HeroAbility::cleave, true), "Cleave cooldown ignored");
    combat.update(heroes, enemies, 0.3, true);
    require(enemies.find(front)->health == 55 && enemies.find(side)->health == 55 &&
                enemies.find(behind)->health == 100 && enemies.find(far)->health == 100,
            "Cleave cone/radius/damage incorrect");
    const auto other = heroes.deploy({-5, 0, 0});
    require(combat.activate(heroes, other, HeroAbility::cleave, true) &&
                heroes.find(hero)->combat.cleaveCooldown != heroes.find(other)->combat.cleaveCooldown,
            "heroes shared ability cooldown state");
    const auto otherTarget = spawn(-5, 1);
    require(combat.attack(heroes, enemies, other, otherTarget) && heroes.find(hero)->combat.target != otherTarget,
            "heroes shared target state");
    require(enemies.remove(otherTarget), "removed hero target fixture failed");
    combat.update(heroes, enemies, 1, true);
    require(heroes.find(other)->combat.target != otherTarget, "removed target retained");
    require(engine.update(), "hero combat render frame failed");

    enemies.clear();
    (void)heroes.deploy({0, 0, 0}, hero);
    const auto firstVictim = spawn(0, 1), secondVictim = spawn(0.7, 1);
    require(enemies.applyDamage(firstVictim, 60) && enemies.applyDamage(secondVictim, 60),
            "Cleave lethal setup failed");
    require(combat.activate(heroes, hero, HeroAbility::cleave, true), "second Cleave activation failed");
    combat.update(heroes, enemies, 0.3, true);
    require(!enemies.find(firstVictim) && !enemies.find(secondVictim), "Cleave skipped a compacted/dead victim");

    enemies.clear();
    (void)heroes.deploy({-3, 0, -2}, hero);
    for (std::uint32_t row = 0; row < 16; ++row) {
        engine.scene()->navigationGrid()->setWalkable({10, row}, false);
    }
    heroes.synchronize(towers);
    const auto unreachable = spawn(2, -2);
    require(combat.attack(heroes, enemies, hero, unreachable), "unreachable attack order not accepted for checking");
    combat.update(heroes, enemies, 0.1, true);
    require(!heroes.find(hero)->combat.target && enemies.find(unreachable)->health == 100,
            "hero retained unreachable target or hit through wall");
    const auto position = heroes.find(hero)->position;
    combat.update(heroes, enemies, 1, true);
    require(heroes.find(hero)->position == position, "unreachable pursuit kept moving");

    enemies.clear();
    const auto fleeing = spawn(-3, -1);
    require(combat.attack(heroes, enemies, hero, fleeing), "pursuit setup failed");
    enemies.setSpeed(5);
    require(enemies.setDestination(fleeing, glm::dvec2{-3, 7}), "fleeing target command failed");
    for (int frame = 0; frame < 120; ++frame) {
        enemies.update(heroes.navigation(), {}, 1.0 / 60);
    }
    combat.update(heroes, enemies, 1, true);
    require(!heroes.find(hero)->combat.target && heroes.find(hero)->combat.excludedTarget == fleeing &&
                heroes.find(hero)->position == position,
            "hero exceeded pursuit limit");
    require(enemies.setDestination(fleeing, glm::dvec2{-3, -1}), "returning target command failed");
    for (int frame = 0; frame < 120; ++frame) {
        enemies.update(heroes.navigation(), {}, 1.0 / 60);
    }
    combat.update(heroes, enemies, 0.8, true);
    require(heroes.find(hero)->combat.target == fleeing && enemies.find(fleeing)->health < 100,
            "hero did not recover when target returned inside acquisition range");
}

void heroTypeChecks(Engine& engine) {
    auto scene = std::make_unique<Scene>("Mixed hero types");
    scene->setNavigationGrid(GridConfiguration{28, 24, 1, {-14, 0, -12}});
    engine.setScene(std::move(scene));
    TowerSystem towers;
    towers.synchronize(engine);
    HeroSystem heroes{engine};
    heroes.synchronize(towers);
    EnemySystem enemies{engine, EnemyDefinition{.despawnOnArrival = false}};
    HeroCombatSystem combat;
    const auto type = [&](std::string_view id) {
        for (std::size_t index = 0; index < heroes.typeCount(); ++index) {
            if (heroes.definition(index).id == id) {
                return index;
            }
        }
        throw std::runtime_error("Missing configured hero type");
    };
    const auto rangerType = type("ranger"), warriorType = type("warrior");
    require(heroes.typeCount() == 3 && heroes.definition().combat.damage == 25 &&
                heroes.definition().combat.attack == HeroAttack::melee,
            "Assassin definition changed or hero catalog is incomplete");
    const auto assassin = heroes.deploy({-10, 0, 0});
    const auto ranger = heroes.deploy({-3, 0, 0}, {}, rangerType);
    const auto rangerPosition = heroes.find(ranger)->position;
    const auto warrior = heroes.deploy({3, 0, 0}, {}, warriorType);
    const auto spawn = [&](double x, double z) {
        require(enemies.spawn(heroes.navigation(), {{x, z}, {x, z}, 0}, 1) == 1, "mixed hero enemy spawn failed");
        return enemies.enemies().back().id;
    };
    const auto rangedTarget = spawn(0, 0);
    const auto front = spawn(3, 1), back = spawn(3, -1), far = spawn(3, 5);
    combat.update(heroes, enemies, 0.6, true);
    require(enemies.find(rangedTarget)->health == 100 - heroes.definition(rangerType).combat.damage,
            "Ranger failed to acquire/fire/hit at range");
    require(glm::length(heroes.find(ranger)->position - rangerPosition) < 1e-6, "in-range Ranger chased unnecessarily");
    require(enemies.find(front)->health == 80 && enemies.find(back)->health == 80 && enemies.find(far)->health == 100,
            "Warrior area attack missed, hit twice, or exceeded radius");
    require(heroes.find(assassin)->combat.target == std::nullopt &&
                heroes.find(ranger)->combat.target != heroes.find(warrior)->combat.target,
            "mixed heroes shared target state");
    require(!combat.activate(heroes, ranger, HeroAbility::cleave, true) &&
                !combat.activate(heroes, warrior, HeroAbility::cleave, true),
            "new hero types inherited an active ability");

    for (const auto id : {ranger, warrior}) {
        require(heroes.command(id, heroes.find(id)->position + glm::dvec3{0, 0, -3}), "mixed hero manual move failed");
    }
    combat.update(heroes, enemies, 0.2, true);
    require(!heroes.find(ranger)->combat.target && !heroes.find(warrior)->combat.target,
            "manual move failed to override autonomous attacks");
    require(heroes.cancelCommand(ranger) && heroes.cancelCommand(warrior), "mixed hero cancel failed");
    combat.update(heroes, enemies, 0.5, true);
    require(heroes.find(ranger)->combat.target && heroes.find(warrior)->combat.target,
            "mixed heroes did not resume autonomous attacks");

    enemies.clear();
    combat.update(heroes, enemies, 0, false);
    (void)heroes.deploy({-8, 0, 0}, ranger);
    const auto distant = spawn(-1, 0);
    require(combat.attack(heroes, enemies, ranger, distant), "Ranger explicit attack failed");
    for (int frame = 0; frame < 300 && enemies.find(distant) && enemies.find(distant)->health == 100; ++frame) {
        combat.update(heroes, enemies, 1.0 / 60, true);
    }
    require(enemies.find(distant) && enemies.find(distant)->health < 100 && heroes.find(ranger)->position.x > -7.9,
            "Ranger failed to approach an out-of-range enemy");

    enemies.clear();
    combat.update(heroes, enemies, 0, false);
    (void)heroes.deploy({3, 0, 0}, warrior);
    const auto lethalFront = spawn(3, 1), lethalBack = spawn(3, -1);
    (void)enemies.applyDamage(lethalFront, 90);
    (void)enemies.applyDamage(lethalBack, 90);
    combat.update(heroes, enemies, 0.6, true);
    require(!enemies.find(lethalFront) && !enemies.find(lethalBack),
            "Warrior lethal area damage skipped a compacted enemy");

    for (const auto id : {assassin, ranger, warrior}) {
        enemies.clear();
        const auto point = heroes.find(id)->position;
        const auto attacker = spawn(point.x, point.z + 0.8);
        const auto health = heroes.find(id)->health;
        EnemyCombatSystem enemyCombat;
        for (int frame = 0; frame < 120 && heroes.find(id)->health == health; ++frame) {
            enemyCombat.prepare(enemies, towers, &heroes, {12, 10}, 1.0 / 60, &heroes.combatGeometry());
            enemies.update(heroes.navigation(), {}, 1.0 / 60);
            enemyCombat.resolve(engine, enemies, towers, &heroes, 1.0 / 60);
        }
        require(enemies.find(attacker) && heroes.find(id)->health < health, "enemy AI failed to damage a hero type");
    }

    // The generic projectile path must sweep terrain and retain the hero attacker domain.
    enemies.clear();
    combat.update(heroes, enemies, 0, false);
    const auto protectedTarget = spawn(0, 0);
    const auto wall = engine.scene()->createObject("Projectile cover");
    engine.scene()->setModel(wall, "models/cube.obj");
    Transform wallTransform;
    wallTransform.position = {-1.5F, 1, 0};
    wallTransform.scale = {0.3F, 2, 2};
    engine.scene()->setTransform(wall, wallTransform);
    engine.prepareScene(*engine.scene());
    heroes.synchronize(towers);
    ProjectileSystem shots{engine};
    shots.spawn({-3, 0.7, 0},
                protectedTarget,
                ProjectileDefinition{18, 12, 0.06},
                0,
                {DefensiveTargetKind::hero, ranger});
    shots.update(enemies, 1, &heroes.combatGeometry());
    require(shots.projectiles().empty() && enemies.find(protectedTarget)->health == 100,
            "hero projectile crossed terrain at low frame rate");
    engine.scene()->removeObject(wall);
    heroes.synchronize(towers);
    shots.spawn({-3, 0.7, 0},
                protectedTarget,
                ProjectileDefinition{18, 12, 0.06},
                0,
                {DefensiveTargetKind::hero, ranger});
    require(shots.projectiles().front().sourceKind == DefensiveTargetKind::hero, "hero projectile lost attacker kind");
    shots.update(enemies, 1, &heroes.combatGeometry());
    require(enemies.find(protectedTarget)->health == 82, "clear hero projectile did not apply damage");
    heroes.clear();
    require(engine.update(), "mixed hero rendering/cleanup failed");
}

void heroDeploymentChecks(Engine& engine) {
    LevelRuntime runtime;
    runtime.load(engine, "assets/levels/first_watch.json");
    const auto originalCount = runtime.heroes()->heroes().size();
    const auto gold = runtime.level().economy().gold();
    for (std::size_t type = 1; type < runtime.heroes()->typeCount(); ++type) {
        runtime.beginHeroPlacement(type);
        bool placed = false;
        for (int x = -8; x <= 8 && !placed; ++x) {
            for (int z = -5; z <= 5 && !placed; ++z) {
                const Ray ray{{x, 15, z}, {0, -1, 0}, 0, 30};
                const auto point = runtime.heroes()->ground(ray);
                if (point && runtime.heroes()->validPosition(*point, {}, type)) {
                    runtime.updateHeroPlacement(engine, ray, true);
                    placed = !runtime.deployingHero();
                }
            }
        }
        require(placed && runtime.selectedHero() && runtime.selectedHero()->type == type &&
                    runtime.level().economy().gold() == gold,
                "typed hero placement failed or changed existing economy rules");
        const auto id = runtime.selectedHero()->id;
        runtime.beginHeroPlacement(type);
        const auto position = runtime.selectedHero()->position;
        runtime.updateHeroPlacement(engine, Ray{position + glm::dvec3{0, 10, 0}, {0, -1, 0}, 0, 20}, true);
        require(runtime.selectedHero()->id == id && runtime.heroes()->heroes().size() == originalCount + type,
                "typed redeployment duplicated or replaced the hero");
    }
    runtime.startWave();
    bool rejected = false;
    try {
        runtime.beginHeroPlacement(1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "typed hero deployment bypassed preparation restriction");
    runtime.restart(engine);
    require(runtime.heroes()->heroes().size() == originalCount && runtime.level().economy().gold() == gold,
            "restart retained runtime hero types or changed gold");
    runtime.unload(engine);
}
} // namespace

int main() {
    try {
        gestures();
        Engine engine{EngineConfig{.windowTitle = "Hero runtime checks"}};
        combatChecks(engine);
        heroTypeChecks(engine);
        heroDeploymentChecks(engine);
        auto scene = std::make_unique<Scene>("Hero obstacle fixture");
        scene->setNavigationGrid(GridConfiguration{16, 12, 1, {-8, 0, -6}});
        for (std::uint32_t row = 0; row < 9; ++row) {
            scene->navigationGrid()->setWalkable({8, row}, false);
        }
        engine.setScene(std::move(scene));
        TowerSystem towers;
        towers.synchronize(engine);
        HeroSystem heroes{engine};
        heroes.synchronize(towers);
        const auto revision = heroes.navigation().revision();
        heroes.synchronize(towers);
        require(heroes.navigation().revision() == revision, "unchanged hero navigation rebuilt");
        require(!heroes.validPosition({0.5, 0, -2}) && !heroes.validPosition({100, 0, 100}),
                "blocked/off-ground hero position accepted");
        const auto id = heroes.deploy({-4, 0, -2});
        const auto object = heroes.find(id)->object;
        const auto other = heroes.deploy({-6, 0, 4});
        require(engine.scene()->animation(object).asset() ==
                    engine.scene()->animation(heroes.find(other)->object).asset(),
                "hero model resources were not shared");
        require(!heroes.validPosition(heroes.find(other)->position, id), "deployment overlapped another hero");
        (void)heroes.deploy({-4, 0, -1}, id);
        require(heroes.find(id)->object == object, "reposition replaced stable hero identity");
        heroes.update(1, false);
        require(heroes.find(id)->state == HeroState::idle, "preparation hero moved");

        for (const int fps : {60, 5, 1}) {
            (void)heroes.deploy({-4, 0, -2}, id);
            require(heroes.command(id, {4, 0, -2}), "hero obstacle route missing");
            for (int frame = 0; frame < fps * 15 && heroes.find(id)->state == HeroState::moving; ++frame) {
                heroes.update(1.0 / fps, true);
                require(heroes.navigation().contains(heroes.find(id)->position), "hero left radius-aware navigation");
            }
            const auto* hero = heroes.find(id);
            require(hero->state == HeroState::reached &&
                        glm::length(glm::dvec2{hero->position.x - 4, hero->position.z + 2}) < 0.15,
                    "hero failed to reach destination around wall at a tested frame rate");
        }
        (void)heroes.deploy({-4, 0, -2}, id);
        require(heroes.command(id, {4, 0, -2}), "retarget setup failed");
        heroes.update(0.1, true);
        require(heroes.command(id, {-6, 0, -3}) && heroes.find(id)->destination->x < -5,
                "new hero command did not replace old destination");
        for (int frame = 0; frame < 180 && heroes.find(id)->state == HeroState::moving; ++frame) {
            heroes.update(1.0 / 60, true);
        }
        require(heroes.find(id)->state == HeroState::reached, "retargeted hero failed to arrive");
        for (std::uint32_t row = 0; row < 12; ++row) {
            engine.scene()->navigationGrid()->setWalkable({8, row}, false);
        }
        heroes.synchronize(towers);
        require(!heroes.command(id, {4, 0, -2}) && heroes.find(id)->state == HeroState::blocked,
                "unreachable hero destination accepted");
        const auto stopped = heroes.find(id)->position;
        heroes.update(2, true);
        require(heroes.find(id)->position == stopped, "unreachable hero kept moving");
        heroes.preview(glm::dvec3{-3, 0, -2}, true);
        require(engine.update(), "hero preview/model frame failed");
        heroes.preview({});
        auto tower = towers.definition("basic").instance();
        tower.position = {-3, tower.halfExtent.y, -2};
        const auto towerId = towers.create(engine, tower);
        heroes.synchronize(towers);
        require(!heroes.validPosition({-3, 0, -2}), "hero deployment accepted tower footprint");
        require(towers.remove(engine, towerId), "hero tower fixture cleanup failed");
        heroes.synchronize(towers);
        require(heroes.validPosition({-3, 0, -2}, id), "removed tower did not restore hero navigation");
        const auto prop = engine.scene()->createObject("Blocking prop");
        engine.scene()->setModel(prop, "models/cube.obj");
        Transform transform;
        transform.position = {-3, 0.5F, -2};
        transform.scale = {0.5F, 0.5F, 0.5F};
        engine.scene()->setTransform(prop, transform);
        engine.prepareScene(*engine.scene());
        heroes.synchronize(towers);
        require(!heroes.validPosition({-3, 0, -2}), "hero ignored a blocking scene prop");
        heroes.clear();
        require(!engine.scene()->contains(object) && heroes.heroes().empty(), "hero cleanup retained scene objects");
        std::cout << "PASS hero deployment, identity, shared models, radius-aware walls/towers/props, retargeting, "
                     "arrival, unreachable goals, 1/5/60 FPS movement and right-click/drag/UI gestures.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Hero: " << error.what() << '\n';
        return 1;
    }
}
