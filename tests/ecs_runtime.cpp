#include "navigation/navigation_session.hpp"
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
} // namespace

int main() {
    try {
        Engine engine{EngineConfig{.windowTitle = "ECS integration checks"}};
        engine.setPerformanceEnabled(true);
        integration(engine);
        std::cout << "PASS ECS enemy/projectile ownership, stale targets, impacts, expiry and level cleanup.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL ECS runtime: " << error.what() << '\n';
        return 1;
    }
}
