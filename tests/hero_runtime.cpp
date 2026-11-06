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
    input.focused = true;
    right = {true, true, false};
    require(!gesture.update(input, true), "right press issued an early command");
    input.mouseButtons[static_cast<std::size_t>(MouseButton::left)].pressed = true;
    right = {false, false, true};
    require(!gesture.update(input, true), "simultaneous selection issued a hero order");
}

void heroAnimationChecks(Engine& engine, std::size_t type) {
    auto scene = std::make_unique<Scene>("Hero animation lifecycle");
    scene->setNavigationGrid(GridConfiguration{16, 16, 1, {-8, 0, -8}});
    engine.setScene(std::move(scene));
    TowerSystem towers;
    towers.synchronize(engine);
    HeroSystem heroes{engine};
    heroes.synchronize(towers);
    const auto id = heroes.deploy({0, 0, 0}, {}, type);
    const auto object = heroes.find(id)->object;
    const auto& animation = engine.scene()->animation(object);
    const auto& model = animation.model();
    require(model.animations.size() == (type == 1 ? 10 : 6) && model.meshes.size() > 1 && !model.skins.empty(),
            "Hero lost clips, mesh parts or skin");
    std::optional<std::size_t> weapon, hand;
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        if (model.nodes[i].name == (type == 0 ? "Knight_Sword" : type == 1 ? "Ranger_Weapon" : "Mage_Weapon")) {
            weapon = i;
        }
        if (model.nodes[i].name == (type == 1 ? "handslot.l" : "handslot.r")) {
            hand = i;
        }
    }
    require(weapon && hand && model.nodes[*weapon].parent == hand, "Knight weapon is not attached to right hand");
    assets::ModelInstance sample{animation.asset()};
    bool handMoved = false;
    for (std::size_t clip = 0; clip < model.animations.size(); ++clip) {
        sample.play(clip, false);
        const auto initial = sample.pose().world[*weapon];
        sample.seek((model.animations[clip].start + model.animations[clip].end) * 0.5F);
        const auto expected = sample.pose().world[*hand] * model.nodes[*weapon].localTransform;
        for (int column = 0; column < 4; ++column) {
            require(glm::length(sample.pose().world[*weapon][column] - expected[column]) < 1e-4F,
                    "Sword detached from animated hand");
            handMoved = handMoved || glm::length(sample.pose().world[*weapon][column] - initial[column]) > 0.01F;
        }
    }
    require(handMoved, "Hero animations did not move the attached weapon");
    if (type == 1) {
        for (std::size_t clip = 0; clip < model.animations.size(); ++clip) {
            if (model.animations[clip].name == "Ranged_Bow_Draw") {
                sample.play(clip, false);
                sample.seek(model.animations[clip].end);
                require(!sample.pose().weights[*weapon].empty() && sample.pose().weights[*weapon][0] > 0.99F,
                        "Bow string did not draw");
            }
            if (model.animations[clip].name == "Ranged_Bow_Release") {
                sample.play(clip, false);
                sample.seek(0.3F);
                require(!sample.pose().weights[*weapon].empty() && sample.pose().weights[*weapon][0] < 0.01F,
                        "Bow string did not release");
            }
        }
    }
    require(model.animations[animation.clip()].name == heroes.definition(type).idleAnimation && animation.looping(),
            "Hero idle missing");
    require(heroes.applyDamage(id, 1), "Hero hit rejected");
    require(model.animations[animation.clip()].name == "Hit_A" && !animation.looping(), "Hero hit missing");
    require(heroes.applyDamage(id, 1), "Hero repeated hit rejected");
    heroes.update(0.7, true);
    require(model.animations[animation.clip()].name == heroes.definition(type).idleAnimation,
            "Hero did not recover from hit");
    require(heroes.applyDamage(id, 1000), "Hero death rejected");
    const auto death = animation.clip();
    require(model.animations[death].name == "Death_A" && !animation.looping(), "Hero death missing");
    EnemySystem enemies{engine};
    HeroCombatSystem combat;
    combat.update(heroes, enemies, 2, true);
    require(animation.clip() == death && engine.scene()->contains(object) && !heroes.command(id, {1, 0, 0}),
            "Hero death interrupted, removed prematurely or accepted a command");
}

void turningChecks(Engine& engine) {
    auto scene = std::make_unique<Scene>("Hero facing and attack timing");
    scene->setNavigationGrid(GridConfiguration{16, 16, 1, {-8, 0, -8}});
    engine.setScene(std::move(scene));
    TowerSystem towers;
    towers.synchronize(engine);
    HeroSystem heroes{engine};
    heroes.synchronize(towers);
    EnemySystem enemies{engine, EnemyDefinition{.despawnOnArrival = false}};
    require(enemies.spawn(heroes.navigation(), {{-1, 0}, {-1, 0}, 0}, 1) == 1, "turning fixture spawn failed");
    const auto target = enemies.enemies().front().id;
    const auto id = heroes.deploy({0, 0, 0});
    HeroCombatSystem combat;
    combat.update(heroes, enemies, 0.05, true);
    const auto* hero = heroes.find(id);
    require(std::isfinite(hero->visualYaw) && hero->visualYaw < 0 && std::abs(hero->visualYaw - hero->yaw) < 1e-6 &&
                enemies.find(target)->health == 100,
            "hero visual rotation snapped or damaged before windup");
    combat.update(heroes, enemies, 0.45, true);
    require(enemies.find(target)->health == 75, "visual rotation changed logical attack timing/damage");
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
    const auto windup = heroes.find(hero)->combat.impactRemaining;
    require(combat.attack(heroes, enemies, hero, target) && combat.attack(heroes, enemies, hero, target) &&
                heroes.find(hero)->combat.swinging && heroes.find(hero)->combat.impactRemaining == windup,
            "repeated attack commands restarted the attack animation/timing");
    combat.update(heroes, enemies, 0.45, true);
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
    require(!combat.activate(heroes, hero, HeroAbility::cleave, true), "Knight retained Cleave");
    combat.update(heroes, enemies, 0.45, true);
    require(enemies.find(interrupted)->health == 75, "rejected ability interrupted basic damage");
    const auto other = heroes.deploy({-5, 0, 0});
    const auto otherTarget = spawn(-5, 1);
    require(combat.attack(heroes, enemies, other, otherTarget) && heroes.find(hero)->combat.target != otherTarget,
            "heroes shared target state");
    require(enemies.remove(otherTarget), "removed hero target fixture failed");
    combat.update(heroes, enemies, 1, true);
    require(heroes.find(other)->combat.target != otherTarget, "removed target retained");
    require(engine.update(), "hero combat render frame failed");

    enemies.clear();
    (void)heroes.deploy({0, 0, 0}, hero);
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
    const auto rangerType = type("ranger"), mageType = type("mage");
    require(heroes.typeCount() == 3 && heroes.definition().combat.damage == 25 &&
                heroes.definition().combat.attack == HeroAttack::melee,
            "Assassin definition changed or hero catalog is incomplete");
    const auto assassin = heroes.deploy({-10, 0, 0});
    const auto ranger = heroes.deploy({-3, 0, 0}, {}, rangerType);
    const auto rangerPosition = heroes.find(ranger)->position;
    const auto mage = heroes.deploy({3, 0, 0}, {}, mageType);
    const auto spawn = [&](double x, double z) {
        require(enemies.spawn(heroes.navigation(), {{x, z}, {x, z}, 0}, 1) == 1, "mixed hero enemy spawn failed");
        return enemies.enemies().back().id;
    };
    const auto rangedTarget = spawn(0, 0);
    const auto front = spawn(3, 1), back = spawn(3.7, 1), far = spawn(3, 5);
    const auto rangerClip = [&] {
        const auto& animation = engine.scene()->animation(heroes.find(ranger)->object);
        return animation.model().animations[animation.clip()].name;
    };
    combat.update(heroes, enemies, 0.1, true);
    require(rangerClip() == "Ranged_Bow_Aiming_Idle", "Ranger did not aim");
    combat.update(heroes, enemies, 0.3, true);
    require(rangerClip() == "Ranged_Bow_Draw" && enemies.find(rangedTarget)->health == 100,
            "Ranger did not draw or fired early");
    const auto windup = heroes.find(ranger)->combat.impactRemaining;
    require(combat.attack(heroes, enemies, ranger, rangedTarget) &&
                heroes.find(ranger)->combat.impactRemaining == windup,
            "Repeated Ranger attack command restarted the draw");
    combat.update(heroes, enemies, 1.5, true);
    require(rangerClip() == "Ranged_Bow_Release", "Ranger did not release");
    require(enemies.find(rangedTarget)->health == 100 - heroes.definition(rangerType).combat.damage,
            "Ranger failed to acquire/fire/hit at range");
    require(glm::length(heroes.find(ranger)->position - rangerPosition) < 1e-6, "in-range Ranger chased unnecessarily");
    require(enemies.find(front)->health == 60 && enemies.find(back)->health == 60 && enemies.find(far)->health == 100,
            "Mage area attack missed, hit twice, or exceeded radius");
    require(heroes.find(assassin)->combat.target == std::nullopt &&
                heroes.find(ranger)->combat.target != heroes.find(mage)->combat.target,
            "mixed heroes shared target state");
    require(!combat.activate(heroes, ranger, HeroAbility::cleave, true) &&
                !combat.activate(heroes, mage, HeroAbility::cleave, true),
            "new hero types inherited an active ability");

    for (const auto id : {ranger, mage}) {
        require(heroes.command(id, heroes.find(id)->position + glm::dvec3{0, 0, -3}), "mixed hero manual move failed");
    }
    combat.update(heroes, enemies, 0.2, true);
    require(!heroes.find(ranger)->combat.target && !heroes.find(mage)->combat.target,
            "manual move failed to override autonomous attacks");
    require(heroes.cancelCommand(ranger) && heroes.cancelCommand(mage), "mixed hero cancel failed");
    combat.update(heroes, enemies, 0.5, true);
    require(heroes.find(ranger)->combat.target && heroes.find(mage)->combat.target,
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
    (void)heroes.deploy({3, 0, 0}, mage);
    const auto lethalFront = spawn(3, 1), lethalBack = spawn(3.7, 1);
    (void)enemies.applyDamage(lethalFront, 90);
    (void)enemies.applyDamage(lethalBack, 90);
    combat.update(heroes, enemies, 1.9, true);
    require(!enemies.find(lethalFront) && !enemies.find(lethalBack),
            "Mage lethal area damage skipped a compacted enemy");

    for (const auto id : {assassin, ranger, mage}) {
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
    engine.scene()->setModel(wall, "towers/structures/primitives/cube.glb");
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
    for (const int fps : {1, 5, 60}) {
        enemies.clear();
        const auto primary = spawn(0, 0), neighbor = spawn(0.7, 0), outside = spawn(3, 0);
        ProjectileDefinition magic{60, 10, 0.09};
        magic.splashRadius = 1.5;
        shots.spawn({-3, 0.7, 0}, primary, magic, 0, {DefensiveTargetKind::hero, mage});
        require(enemies.find(primary)->health == 100 && enemies.find(neighbor)->health == 100,
                "Mage damaged enemies before projectile impact");
        for (int frame = 0; frame < fps; ++frame) {
            shots.update(enemies, 1.0 / fps, &heroes.combatGeometry());
        }
        require(enemies.find(primary)->health == 40 && enemies.find(neighbor)->health == 40 &&
                    enemies.find(outside)->health == 100 && shots.projectiles().empty(),
                "Mage splash damage duplicated, missed or varied with frame rate");
        shots.spawn({-3, 0.7, 0}, primary, magic, 0, {DefensiveTargetKind::hero, mage});
        shots.update(enemies, 1, &heroes.combatGeometry());
        require(!enemies.find(primary) && !enemies.find(neighbor) && enemies.find(outside),
                "Lethal splash skipped a compacted enemy");
        shots.spawn({0, 0.7, 0}, outside, magic);
        require(enemies.remove(outside), "Stale projectile target setup failed");
        const auto replacement = spawn(3, 0);
        shots.update(enemies, 1, &heroes.combatGeometry());
        require(enemies.find(replacement)->health == 100 && shots.projectiles().empty(),
                "Projectile followed a recycled enemy generation");
    }
    heroes.clear();
    require(engine.update(), "mixed hero rendering/cleanup failed");
}

void heroDeploymentChecks(Engine& engine) {
    LevelRuntime runtime;
    runtime.load(engine, "assets/levels/first_watch.json");
    auto& heroes = *runtime.heroes();
    const auto gold = runtime.level().economy().gold();
    const auto& definition = runtime.level().waves()->definition();
    const glm::dvec3 objective{definition.objectivePosition.x,
                               definition.objectiveHeight,
                               definition.objectivePosition.y};
    require(heroes.heroes().size() == 3, "Full party did not spawn automatically");
    for (std::size_t type = 0; type < 3; ++type) {
        const auto& hero = heroes.heroes()[type];
        require(hero.type == type && heroes.navigation(type).contains(hero.position) &&
                    glm::length(hero.position - objective) <= 6.5,
                "Hero spawned outside clear ground near the objective");
    }
    const auto first = heroes.heroes().front().id;
    const auto initial = heroes.find(first)->position;
    const auto second = heroes.heroes()[1].position;
    require(runtime.selectHero(first), "Automatic hero was not selectable");
    require(runtime.commandHero({{-3, 15, -1}, {0, -1, 0}, 0, 30}), "Preparation move rejected");
    runtime.update(engine, 0.5);
    require(glm::length(heroes.find(first)->position - initial) > 0.1 && heroes.heroes()[1].position == second,
            "Preparation move failed or moved the other heroes");
    runtime.update(engine, 8);
    require(!heroes.find(first)->destination &&
                glm::length(heroes.find(first)->position - glm::dvec3{-3, 0, -1}) < 0.15 &&
                runtime.level().economy().gold() == gold,
            "Hero failed to complete its free preparation move");
    runtime.restart(engine);
    require(runtime.heroes()->heroes().size() == 3 && !runtime.selectedHero() &&
                runtime.level().economy().gold() == gold,
            "Restart did not restore the full party");
    runtime.unload(engine);
}
} // namespace

int main() {
    try {
        gestures();
        Engine engine{EngineConfig{.windowTitle = "Hero runtime checks"}};
        for (std::size_t type = 0; type < 3; ++type) {
            heroAnimationChecks(engine, type);
        }
        turningChecks(engine);
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
        const auto queries = heroes.navigation().statistics().pathQueries;
        require(heroes.command(id, {4, 0, -2}) && heroes.navigation().statistics().pathQueries == queries,
                "repeated move order rebuilt an unchanged path");
        const auto originalDestination = heroes.find(id)->destination;
        require(!heroes.command(id, {1000, 0, 1000}) && heroes.find(id)->state == HeroState::moving &&
                    heroes.find(id)->destination == originalDestination && heroes.find(id)->combat.playerMoving,
                "invalid move command cancelled a valid order");
        heroes.update(0.1, true);
        require(heroes.command(id, {-6, 0, -3}) && heroes.find(id)->destination->x < -5,
                "new hero command did not replace old destination");
        for (int frame = 0; frame < 180 && heroes.find(id)->state == HeroState::moving; ++frame) {
            heroes.update(1.0 / 60, true);
        }
        require(heroes.find(id)->state == HeroState::reached, "retargeted hero failed to arrive");
        require(heroes.command(id, {-6, 0, -1}), "cancel order fixture failed");
        heroes.update(0.1, true);
        require(heroes.cancelCommand(id), "cancel move failed");
        const auto cancelledAt = heroes.find(id)->position;
        EnemySystem emptyEnemies{engine};
        HeroCombatSystem idleCombat;
        idleCombat.update(heroes, emptyEnemies, 1, true);
        require(heroes.find(id)->position == cancelledAt && heroes.find(id)->defensivePosition == cancelledAt,
                "autonomous defense resumed the cancelled movement order");
        for (std::uint32_t row = 0; row < 12; ++row) {
            engine.scene()->navigationGrid()->setWalkable({8, row}, false);
        }
        heroes.synchronize(towers);
        require(!heroes.command(id, {4, 0, -2}) && heroes.find(id)->state == HeroState::idle,
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
        engine.scene()->setModel(prop, "towers/structures/primitives/cube.glb");
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
