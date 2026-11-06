#include "allocation_counter.hpp"
#include "navigation/navigation_session.hpp"
#include "runtime/level_runtime.hpp"
#include "systems/projectile_system.hpp"

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

void integration(Engine& engine) {
    auto scene = std::make_unique<Scene>("ECS runtime integration");
    scene->setNavigationGrid(GridConfiguration{16, 16, 1, {-8, 0, -8}});
    engine.setScene(std::move(scene));
    NavigationSession navigation;
    navigation.synchronize(engine);
    auto& enemies = *navigation.enemies();
    const auto spawn = [&](glm::dvec2 position) {
        require(navigation.spawnAt(engine, 0, {position, position, 0}, 1) == 1, "ECS enemy fixture failed to spawn");
        return enemies.enemies().back().id;
    };
    const auto first = spawn({-2, -2});
    const auto second = spawn({2, 2});
    require(enemies.registry().size() == 2 &&
                enemies.registry().get<EnemyRuntimeComponent>(first) == enemies.find(first) &&
                enemies.registry().get<TeamComponent>(first)->team == Team::enemy,
            "EnemySystem did not expose authoritative ECS components");
    ProjectileSystem projectiles{engine};
    auto definition = navigation.towers().definition("basic");
    const auto origin = combatAim(*enemies.find(first)) - glm::dvec3{1, 0, 0};
    const auto health = enemies.find(first)->health;
    projectiles.spawn(origin, first, definition);
    const auto projectile = projectiles.registry().entities<ProjectileComponent>().front();
    require(projectiles.registry().has<ProjectileLifetimeComponent>(projectile) && projectiles.registry().size() == 1,
            "Projectile components were not owned by the ECS");
    projectiles.update(enemies, 1, &navigation.combatGeometry());
    require(projectiles.projectiles().empty() && !projectiles.registry().valid(projectile) &&
                enemies.find(first)->health == health - definition.statistics(1).damage,
            "ECS projectile impact changed damage or left live components");

    projectiles.spawn(origin, first, definition);
    require(enemies.remove(first), "enemy removal failed");
    const auto replacement = spawn({-2, -2});
    require(ecs::entityIndex(replacement) == ecs::entityIndex(first) && replacement != first && !enemies.find(first),
            "Enemy slot reuse failed to invalidate the old generation");
    const auto replacementHealth = enemies.find(replacement)->health;
    const auto replacementOrder = enemyOrder(*enemies.find(replacement));
    const auto later = spawn({-4, 4});
    require(later < replacement && enemyOrder(*enemies.find(later)) > replacementOrder,
            "gameplay ordering incorrectly followed recycled handle bits");
    projectiles.update(enemies, 1, &navigation.combatGeometry());
    require(projectiles.projectiles().empty() && enemies.find(replacement)->health == replacementHealth,
            "A stale projectile damaged the replacement enemy");

    projectiles.spawn({0, 8, 0}, replacement, definition, 1, 0, 0.02);
    const auto expired = projectiles.registry().entities<Projectile>().front();
    projectiles.update(enemies, 0.03, &navigation.combatGeometry());
    require(!projectiles.registry().valid(expired) && projectiles.registry().size() == 0 &&
                enemies.find(replacement)->health == replacementHealth,
            "Expired projectile retained its entity or caused damage");

    definition.levels[0].damage = 1000;
    for (int index = 0; index < 20; ++index) {
        projectiles.spawn(combatAim(*enemies.find(second)), second, definition);
    }
    projectiles.update(enemies, 1, &navigation.combatGeometry());
    require(!enemies.find(second) && !enemies.registry().has<TeamComponent>(second) &&
                projectiles.projectiles().empty() && projectiles.registry().size() == 0 &&
                enemies.metrics().defeated == 1,
            "Simultaneous impacts left stale references, components, or duplicate deaths");
    enemies.updateEffects(100);
    navigation.reset(engine);
    require(enemies.registry().size() == 0 && !enemies.registry().valid(replacement),
            "level reset retained enemy entities");
    const auto restarted = spawn({-2, -2});
    require(!enemies.find(replacement) && enemies.registry().valid(restarted) &&
                enemyOrder(*enemies.find(restarted)) > replacementOrder,
            "reset resurrected a stale handle or reset spawn ordering");
    enemies.clear();
    projectiles.clear();
    navigation.shutdown();
    require(engine.update() && engine.performance().instances == 0, "ECS cleanup retained renderer instances");
}

void heroesAndTowers(Engine& engine) {
    auto scene = std::make_unique<Scene>("Hero and tower ECS checks");
    scene->setNavigationGrid(GridConfiguration{24, 24, 1, {-12, 0, -12}});
    engine.setScene(std::move(scene));
    NavigationSession navigation;
    navigation.synchronize(engine);
    HeroSystem heroes{engine};
    heroes.synchronize(navigation.towers());
    const auto oldHero = heroes.deploy({-6, 0, -4});
    const auto oldObject = heroes.find(oldHero)->object;
    require(heroes.registry().get<HeroRuntimeComponent>(oldHero) == heroes.find(oldHero) &&
                heroes.registry().get<TeamComponent>(oldHero)->team == Team::player,
            "hero data is not ECS owned");
    require(heroes.remove(oldHero) && !engine.scene()->contains(oldObject), "hero removal retained its Scene object");
    const auto hero = heroes.deploy({-6, 0, -4});
    require(hero != oldHero && ecs::entityIndex(hero) == ecs::entityIndex(oldHero) &&
                !heroes.command(oldHero, {-5, 0, -4}),
            "recycled hero accepted a stale command");
    const auto object = heroes.find(hero)->object;
    (void)heroes.deploy({-6, 0, -3}, hero);
    require(heroes.find(hero)->object == object && heroes.command(hero, {-5, 0, -3}),
            "hero reposition lost identity or movement");
    for (int frame = 0; frame < 120; ++frame) {
        heroes.update(1.0 / 60, true);
    }
    require(heroes.find(hero)->state == HeroState::reached, "ECS hero failed to complete its movement command");
    const auto external = heroes.deploy({-7, 0, 5});
    engine.scene()->removeObject(heroes.find(external)->object);
    require(!heroes.find(external) && !heroes.command(external, {-7, 0, 4}),
            "deleted Scene hero accepted a stale command before synchronization");
    heroes.synchronize(navigation.towers());
    require(!heroes.registry().valid(external), "Scene synchronization retained a deleted hero entity");

    auto& towers = navigation.towers();
    auto configured = towers.definition("basic").instance();
    const auto create = [&](double x) {
        auto tower = configured;
        tower.position = {x, tower.halfExtent.y, 0};
        return towers.create(engine, tower);
    };
    const auto first = create(-4), middle = create(0), last = create(4);
    navigation.synchronize(engine);
    const auto previous = towers.entityForObject(middle);
    const auto revision = navigation.revision();
    require(navigation.removeTower(engine, middle) && !towers.find(previous) && navigation.revision() != revision,
            "tower deletion retained its ECS body or navigation obstacle");
    require(towers.towers()[0].object == first && towers.towers()[1].object == last,
            "tower deletion changed deterministic processing order");
    const auto replacement = create(0);
    const auto entity = towers.entityForObject(replacement);
    require(entity != previous && ecs::entityIndex(entity) == ecs::entityIndex(previous) &&
                !towers.registry().has<TowerProgress>(previous) &&
                towers.registry().get<Tower>(entity) == towers.find(entity),
            "tower slot reuse retained a stale component");
    EconomySystem economy;
    economy.reset(1000);
    towers.upgrade(replacement, economy);
    require(towers.progress(replacement).level == 2 && towers.progress(first).level == 1,
            "ECS tower upgrade affected another instance");
    require(towers.applyDamage(engine, replacement, 5), "ECS tower rejected damage");
    const auto health = towers.progress(replacement).health;
    engine.scene()->setEnabled(replacement, false);
    navigation.synchronize(engine);
    require(!towers.find(entity) && towers.registry().valid(entity), "disabled tower kept an active obstacle body");
    engine.scene()->setEnabled(replacement, true);
    navigation.synchronize(engine);
    require(towers.entityForObject(replacement) == entity && towers.progress(replacement).health == health &&
                towers.progress(replacement).level == 2,
            "reenabling a tower reset its runtime health or upgrade");
    const auto& wide = towers.definition("wide");
    engine.scene()->setRole(replacement, "grimlock.tower:wide");
    engine.scene()->setModel(replacement, wide.model);
    Transform edited;
    edited.position = {1, static_cast<float>(wide.halfExtent.y), 0};
    edited.scale = wide.visualScale;
    engine.scene()->setTransform(replacement, edited);
    navigation.synchronize(engine);
    require(towers.find(entity)->type == "wide" && towers.find(entity)->halfExtent == wide.halfExtent &&
                towers.find(entity)->position.x == 1 && towers.progress(replacement).level == 1 &&
                towers.progress(replacement).health == wide.maximumHealth,
            "cached tower projection ignored edited type, transform or health definition");
    require(towers.applyDamage(engine, replacement, 100000) && !towers.registry().valid(entity),
            "tower death retained its ECS identity");
    navigation.synchronize(engine);
    require(heroes.applyDamage(hero, 100000) && !heroes.command(hero, {-4, 0, -3}), "dead hero accepted movement");
    heroes.clear();
    require(!heroes.registry().valid(hero) && heroes.heroes().empty(), "hero reset retained ECS components");
    navigation.shutdown();
}

void selection(Engine& engine) {
    LevelRuntime runtime;
    runtime.load(engine, "assets/levels/first_watch.json");
    const auto initialHeroes = runtime.heroes()->heroes().size();
    const auto initialTowers = runtime.navigation().towers().towers().size();
    const auto hero = runtime.heroes()->heroes().front().id;
    require(runtime.selectHero(hero) && runtime.heroes()->remove(hero) && !runtime.selectedHero(),
            "removed hero remained selected");
    auto tower = runtime.navigation().towers().definition("basic").instance();
    tower.position = {3, tower.halfExtent.y, 0};
    const auto object = runtime.navigation().towers().create(engine, tower);
    runtime.navigation().synchronize(engine);
    runtime.selectObject(engine, Ray{tower.position + glm::dvec3{0, 8, 0}, {0, -1, 0}, 0, 20});
    require(runtime.inspectedTower() && runtime.inspectedTower()->object == object,
            "runtime tower selection did not resolve ECS identity");
    require(runtime.navigation().removeTower(engine, object) && !runtime.inspectedTower(),
            "removed tower remained selected");
    runtime.restart(engine);
    require(!runtime.selectedHero() && !runtime.inspectedTower() &&
                runtime.heroes()->heroes().size() == initialHeroes &&
                runtime.navigation().towers().towers().size() == initialTowers,
            "restart retained selections or runtime actors");
    runtime.unload(engine);
    for (const auto* path :
         {"assets/levels/first_watch.json", "assets/levels/hill_watch.json", "assets/levels/first_watch.json"}) {
        runtime.load(engine, path);
        require(runtime.heroes()->heroes().size() == runtime.heroes()->typeCount() &&
                    runtime.navigation().enemies()->registry().size() == 0 && !runtime.selectedHero() &&
                    !runtime.inspectedTower(),
                "repeated level load retained entities or selection");
        for (const auto& tower : runtime.navigation().towers().towers()) {
            require(runtime.navigation().towers().progress(tower.object).level == 1,
                    "repeated load retained runtime upgrades");
        }
        runtime.startWave();
        runtime.update(engine, 1);
        runtime.unload(engine);
        require(engine.update() && engine.performance().instances == 0,
                "battle unload retained standalone render instances");
    }
}

void reactivationFailures(Engine& engine) {
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL != 0
    // Checked STL containers can allocate proxy state in noexcept operations.
    // Broad failure injection must run without those implementation allocations.
    std::cout << "Tower allocation-failure injection requires unchecked STL; covered by Release checks.\n";
    return;
#endif
    engine.setScene(std::make_unique<Scene>("ECS allocation rollback"));
    TowerSystem towers;
    towers.synchronize(engine);
    auto tower = towers.definition("basic").instance();
    tower.position = {0, tower.halfExtent.y, 0};
    const auto object = towers.create(engine, tower);
    const auto entity = towers.entityForObject(object);
    int failures = 0;
    for (int round = 0; round < 12; ++round) {
        engine.scene()->setEnabled(object, false);
        towers.synchronize(engine);
        auto additional = tower;
        additional.position.x = 2.0 * (round + 1);
        (void)towers.create(engine, additional);
        bool completed = false;
        for (int point = 0; point < 128 && !completed; ++point) {
            engine.scene()->setEnabled(object, false);
            towers.synchronize(engine);
            engine.scene()->setEnabled(object, true);
            try {
                checks::AllocationFailure failure{point};
                towers.synchronize(engine);
                completed = true;
            } catch (const std::bad_alloc&) {
                ++failures;
            }
            require(!towers.registry().has<Tower>(entity) || towers.registry().has<TowerAttack>(entity),
                    "failed tower reactivation exposed a body without attack state");
            require(towers.registry().valid(entity) &&
                        towers.progress(object).health == towers.definition("basic").maximumHealth,
                    "failed reactivation lost retained tower identity/health");
        }
        require(completed, "allocation rollback fixture never recovered");
    }
    std::cout << "Tower reactivation recovered from " << failures << " injected allocation failures.\n";
}
} // namespace

int main() {
    try {
        Engine engine{EngineConfig{.windowTitle = "ECS integration checks"}};
        engine.setPerformanceEnabled(true);
        integration(engine);
        reactivationFailures(engine);
        heroesAndTowers(engine);
        selection(engine);
        std::cout << "PASS ECS enemy/projectile ownership, stale targets, impacts, expiry and level cleanup.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL ECS runtime: " << error.what() << '\n';
        return 1;
    }
}
