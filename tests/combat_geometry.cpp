#include "systems/combat_geometry.hpp"

#include "runtime/level_runtime.hpp"

#include <chrono>
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

void geometryChecks() {
    NavigationGeometry geometry;
    geometry.addTriangle({0, -2, -2}, {0, 2, -2}, {0, 0, 2}, false);
    CombatGeometry world;
    world.build(geometry);
    const auto hit = world.trace({-10, 0, 0}, {10, 0, 0});
    require(hit && std::abs(hit->fraction - 0.5) < 1e-8, "thin triangle did not stop a high-speed segment");
    require(world.trace({10, 0, 0}, {-10, 0, 0}).has_value(), "back faces did not obstruct combat");
    require(!world.trace({-10, 3, 0}, {10, 3, 0}), "clear ray was obstructed");
    const auto sweep = world.trace({-10, 0, 0}, {10, 0, 0}, 0.1);
    require(sweep && sweep->fraction < hit->fraction, "projectile radius did not contact the triangle face");
    require(world.trace({-1, 2.04, -2}, {1, 2.04, -2}, 0.06).has_value(), "sweep missed a grazing triangle vertex");
    require(!world.trace({-1, 2.2, -2}, {1, 2.2, -2}, 0.06), "sweep inflated triangle edges excessively");
    bool rejected = false;
    try {
        (void)world.trace({0, 0, 0}, {1, 0, 0}, -1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "invalid sweep radius accepted");

    NavigationGeometry many;
    for (int i = 0; i < 10000; ++i) {
        const float x = static_cast<float>(i % 100) * 2, z = static_cast<float>(i / 100) * 2;
        many.addTriangle({x, 0, z}, {x, 2, z}, {x, 0, z + 1}, false);
    }
    world.build(many);
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 20000; ++i) {
        require(world.trace({-1, 0.5, 0.2}, {199, 0.5, 0.2}).has_value(), "BVH query lost an obstruction");
    }
    std::cout << "20,000 combat rays / 10,000 triangles: "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()
              << " ms\n";
}

void runtimeChecks(Engine& engine) {
    auto scene = std::make_unique<Scene>("Combat obstruction checks");
    scene->setNavigationGrid(GridConfiguration{16, 12, 1, {-8, 0, -6}});
    engine.setScene(std::move(scene));
    TowerSystem towers;
    towers.synchronize(engine);
    auto tower = towers.definition("basic").instance();
    tower.position = {-2, tower.halfExtent.y, 0};
    const auto source = towers.create(engine, tower);
    tower.object = source;
    NavigationGeometry ground;
    ground.addBox({-8, -0.2F, -6}, {8, 0, 6}, true);
    NavMesh mesh;
    require(mesh.build(ground), "could not build combat fixture navigation");
    EnemyDefinition definition;
    definition.speed = 0;
    definition.maximumHealth = 10000;
    definition.despawnOnArrival = false;
    EnemySystem enemies{engine, definition};
    const auto spawn = [&](double x, double z) {
        require(enemies.spawn(mesh, {{x, z}, {x, z}, 0}, 1) == 1, "LOS target spawn failed");
        return enemies.enemies().back().id;
    };
    const auto near = spawn(0, 0);
    const auto other = spawn(0, 2);
    CombatGeometry world;
    world.synchronize(engine, towers.towers());
    const auto revision = world.revision();
    world.synchronize(engine, towers.towers());
    require(revision == world.revision(), "unchanged combat geometry rebuilt");
    require(world.visible(firingOrigin(tower), combatAim(*enemies.find(near)), source),
            "own tower blocked its firing origin");
    CombatSystem combat{engine};
    combat.update(towers, enemies, 0.01, &world);
    require(combat.attacks()[0].target == near, "nearest visible target was not acquired");
    const auto blocker = engine.scene()->createObject("Thin solid wall");
    engine.scene()->setModel(blocker, "models/cube.obj");
    engine.scene()->setRole(blocker, "grimlock.obstacle");
    Transform transform;
    transform.position = {-1, 1, 0};
    transform.scale = {0.015F, 1, 0.25F};
    engine.scene()->setTransform(blocker, transform);
    world.synchronize(engine, towers.towers());
    require(!world.visible(firingOrigin(tower), combatAim(*enemies.find(near)), source),
            "thin wall did not obstruct target");
    combat.update(towers, enemies, 0.01, &world);
    require(combat.attacks()[0].target == other, "tower did not replace an obstructed target");
    const auto health = enemies.find(near)->health;
    ProjectileSystem projectiles{engine};
    auto fast = towers.definition("basic");
    fast.projectileSpeed = 1000;
    projectiles.spawn(firingOrigin(tower), near, fast, 1, source);
    projectiles.update(enemies, 0.5, &world);
    require(projectiles.projectiles().empty() && enemies.find(near)->health == health && projectiles.impacts() == 0,
            "fast projectile tunneled through a thin wall");
    engine.scene()->setRole(blocker, "grimlock.decoration");
    world.synchronize(engine, towers.towers());
    require(world.visible(firingOrigin(tower), combatAim(*enemies.find(near)), source),
            "decorative geometry blocked combat");
    combat.update(towers, enemies, 0.01, &world);
    require(combat.attacks()[0].target == other, "valid target switched unnecessarily after visibility changed");
    require(enemies.remove(other), "could not remove tracked target");
    combat.update(towers, enemies, 0.01, &world);
    require(combat.attacks()[0].target == near, "tower did not reacquire after target death");
    projectiles.spawn(firingOrigin(tower), near, fast, 1, source);
    projectiles.update(enemies, 0.5, &world);
    require(enemies.find(near)->health == health - fast.statistics().damage, "clear projectile lost damage behavior");
    auto second = towers.definition("wide").instance();
    second.position = {-1, second.halfExtent.y, 0};
    const auto obstacle = towers.create(engine, second);
    world.synchronize(engine, towers.towers());
    require(!world.visible({-2, 0.5, 0}, {0, 0.5, 0}, source), "constructed tower was not a dynamic blocker");
    require(world.visible({-2, 0.5, 0}, {-1, 0.5, 0}, source, obstacle),
            "target's own footprint obstructed a melee ray");
    require(towers.remove(engine, obstacle), "dynamic blocker removal failed");
    world.synchronize(engine, towers.towers());
    require(world.visible({-2, 0.5, 0}, {0, 0.5, 0}, source), "removed tower left a stale combat blocker");
    require(engine.update(), "combat geometry rendering failed");
}
} // namespace

int main() {
    try {
        geometryChecks();
        Engine engine{EngineConfig{.windowTitle = "Combat visibility checks", .width = 640, .height = 480}};
        runtimeChecks(engine);
        LevelRuntime runtime;
        runtime.load(engine, "assets/levels/cover_watch.json");
        require(runtime.navigation().combatGeometry().revision() > 0 && runtime.heroes()->heroes().size() == 1,
                "terrain combat scene did not load");
        runtime.startWave();
        for (int i = 0; i < 1800 && runtime.level().waves()->phase() == LevelPhase::battle; ++i) {
            runtime.update(engine, 0.1);
        }
        require(runtime.level().waves()->phase() != LevelPhase::battle, "covered elevated battle stalled");
        require(engine.update(), "covered hill battle failed to render");
        std::cout << "PASS combat BVH, sweeps, cover, stable targeting, dynamic obstacles and elevated battle.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
