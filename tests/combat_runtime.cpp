#include "systems/combat_system.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace grimlock;
using namespace grimlock::game;

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

EnemyId spawn(EnemySystem& enemies, const NavMesh& mesh, double x, double z = 0) {
    require(enemies.spawn(mesh, {{x, z}, {x, z}, 0}, 1) == 1, "combat enemy spawn failed");
    return enemies.enemies().back().id;
}

void checkTowerVisuals(Engine& engine, TowerSystem& towers, EnemySystem& enemies, const NavMesh& mesh) {
    for (const auto* type : {"basic", "wide"}) {
        const auto& configured = towers.definition(type);
        auto tower = configured.instance();
        tower.position = {-5, tower.halfExtent.y, -5};
        const auto root = towers.create(engine, tower);
        const auto children = engine.scene()->children(root);
        require(children.size() == 1, "Tower must own exactly one visual defender");
        const auto child = children[0];
        require(engine.scene()->object(child).model == configured.operatorModel &&
                    engine.scene()->object(child).role == "grimlock.tower_operator" &&
                    !engine.scene()->animation(child).model().skins.empty(),
                "Tower defender lost its hero model, equipment or skeleton");
        const auto revision = towers.revision();
        const auto resting = engine.scene()->object(child).local;
        towers.synchronize(engine);
        require(engine.scene()->children(root).size() == 1 && towers.revision() == revision,
                "Tower visual synchronization duplicated its defender or rebuilt collision");
        towers.updateOperator(engine, root, glm::dvec3{-3, 0, -4}, true, 0);
        const auto& playback = engine.scene()->animation(child);
        require(!playback.looping() && playback.model().animations[playback.clip()].name == configured.operatorAttack,
                "Tower did not play its actual hero attack clip");
        towers.updateOperator(engine, root, {}, false, 0);
        const auto returned = engine.scene()->object(child).local;
        require(glm::length(returned.position - resting.position) < 1e-5F &&
                    std::abs(glm::dot(returned.rotation, resting.rotation)) > 0.9999F,
                "Tower defender did not return to its authored position and facing after losing its target");
        require(playback.looping() && playback.model().animations[playback.clip()].name == configured.operatorIdle,
                "Tower defender did not return to idle");
        require(towers.remove(engine, root) && !engine.scene()->contains(child),
                "Destroyed tower retained its visual defender");
    }
    const auto id = spawn(enemies, mesh, 2);
    ProjectileSystem projectiles{engine};
    projectiles.spawn({0, 2, 0}, id, towers.definition("wide"));
    require(projectiles.projectiles().size() == 1 && projectiles.projectiles()[0].damageType == DamageType::magic &&
                projectiles.projectiles()[0].visualScale == 0.16F,
            "Mage tower did not use its configured magic fireball");
    const auto& mage = towers.definition("wide");
    require(mage.projectileModel == "towers/projectiles/fireball_purple.glb" &&
                engine.modelInfo(engine.loadModel(engine.assetRoot() / mage.projectileModel)).primitives == 2,
            "Purple fireball lost its luminous core or flame tongues");
    projectiles.clear();
    enemies.clear();
}

void checkProjectiles(Engine& engine, EnemySystem& enemies, const NavMesh& mesh, const TowerDefinition& small) {
    const auto id = spawn(enemies, mesh, 2);
    const auto handle = *enemies.find(id)->instance;
    const auto position = enemies.find(id)->position;
    require(enemies.find(id)->health == 100 && enemies.find(id)->maximumHealth == 100,
            "spawned enemy health incorrect");
    ProjectileSystem projectiles{engine};
    projectiles.spawn({0, 1.6, 0}, id, small);
    projectiles.update(enemies, 0.01);
    require(projectiles.projectiles().size() == 1 && enemies.find(id)->health == 100,
            "projectile damaged enemy before reaching it");
    require(engine.transform(projectiles.projectiles()[0].instance)[3].x > 0, "projectile instance did not move");
    projectiles.update(enemies, 1);
    require(projectiles.projectiles().empty() && enemies.find(id)->health == 75 && projectiles.impacts() == 1,
            "low-FPS projectile tunneled or dealt incorrect damage");
    enemies.updateEffects(0.2);
    require(enemies.find(id)->hitFlashRemaining == 0, "hit feedback did not expire");
    require(enemies.applyDamage(id, 75) && !enemies.find(id) && enemies.deathEffectCount() == 1 &&
                enemies.metrics().defeated == 1 && enemies.metrics().arrived == 0,
            "death retained a targetable/navigating enemy or counted as arrival");
    const auto transform = engine.transform(handle);
    enemies.update(mesh, std::nullopt, 0.01);
    require(engine.transform(handle) == transform && position.x == 2 && !enemies.applyDamage(id, 100),
            "dead enemy moved or accepted repeated damage");
    auto& playback = engine.animation(handle);
    const auto& names = playback.model().animations;
    const bool hasDeath = std::any_of(names.begin(), names.end(), [](const auto& clip) {
        auto name = clip.name;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return name.find("death") != std::string::npos || name.find("die") != std::string::npos;
    });
    require(!hasDeath || (playback.playing() && !playback.looping()), "available death animation did not play once");
    enemies.updateEffects(60);
    bool invalid = false;
    try {
        (void)engine.transform(handle);
    } catch (const std::out_of_range&) {
        invalid = true;
    }
    require(invalid && enemies.deathEffectCount() == 0, "death effect retained its instance");

    const auto removed = spawn(enemies, mesh, 3);
    projectiles.spawn({0, 1, 0}, removed, small);
    require(enemies.remove(removed), "target removal failed");
    const auto replacement = spawn(enemies, mesh, 3);
    require(removed != replacement, "enemy IDs reused after removal");
    projectiles.update(enemies, 5);
    require(projectiles.projectiles().empty() && enemies.find(replacement)->health == 100,
            "stale projectile hit a replacement enemy");
    ProjectileDefinition whiteArrow;
    whiteArrow.model = "towers/projectiles/arrow_white.glb";
    whiteArrow.visualScale = 0.5F;
    whiteArrow.tint = {1, 1, 1, 1};
    projectiles.spawn({0, 1, 0}, replacement, whiteArrow);
    require(projectiles.projectiles().front().whiteArrowTrail, "White arrow did not enable its trail");
    for (int frame = 0; frame < 60; ++frame) {
        projectiles.update(enemies, 0.01);
        require(engine.update(), "White arrow or tapering trail failed to render");
    }
    require(projectiles.projectiles().empty() && enemies.find(replacement)->health == 99,
            "White arrow changed hit damage or did not finish its flight");
    projectiles.spawn({0, 1, 0}, replacement, whiteArrow);
    projectiles.update(enemies, 0.01);
    projectiles.clear();
    require(engine.update() && projectiles.projectiles().empty(), "Arrow trail cleanup failed");
    bool rejected = false;
    try {
        (void)enemies.applyDamage(replacement, std::numeric_limits<float>::quiet_NaN());
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "invalid damage accepted");
    enemies.clear();
}

void checkTowers(Engine& engine, EnemySystem& enemies, const NavMesh& mesh, TowerSystem& towers) {
    auto small = towers.definition("basic").instance();
    small.position = {0, small.halfExtent.y, 0};
    const auto tower = towers.create(engine, small);
    const auto distant = spawn(enemies, mesh, 9);
    const auto farther = spawn(enemies, mesh, 3);
    const auto nearest = spawn(enemies, mesh, 1.5);
    CombatSystem combat{engine};
    combat.update(towers, enemies, 0.01);
    require(towers.attacks().size() == 1 && towers.attacks()[0].target == nearest && combat.shotsFired() == 1 &&
                combat.projectiles().projectiles().size() == 1,
            "tower did not fire at the nearest in-range target");
    require(engine.update(), "combat rendering frame failed");
    require(enemies.remove(nearest), "tracked target removal failed");
    combat.update(towers, enemies, 0.01);
    require(towers.attacks()[0].target == farther, "tower did not reacquire a valid target");
    const glm::dvec2 goal{8, 0};
    enemies.setSpeed(12);
    require(enemies.setDestination(farther, goal), "moving target destination failed");
    for (int frame = 0; frame < 600 && enemies.find(farther) &&
                        enemies.find(farther)->position.x <= towers.definition("basic").statistics().attackRange + 0.1;
         ++frame) {
        enemies.update(mesh, std::nullopt, 1.0 / 60);
    }
    combat.update(towers, enemies, 0.01);
    require(!towers.attacks()[0].target && enemies.find(farther) &&
                enemies.find(farther)->position.x > towers.definition("basic").statistics().attackRange,
            "tower kept tracking an enemy after it left attack range");
    enemies.setSpeed(0);
    require(enemies.remove(farther), "second target removal failed");
    combat.update(towers, enemies, 1);
    require(!towers.attacks()[0].target && enemies.find(distant)->health == 100 && combat.shotsFired() == 1,
            "tower attacked an out-of-range enemy");
    require(towers.remove(engine, tower), "tower removal failed");
    combat.update(towers, enemies, 0.1);
    require(towers.attacks().empty(), "removed tower retained attack state");
    enemies.clear();

    small.position.z = -1;
    const auto first = towers.create(engine, small);
    auto large = towers.definition("wide").instance();
    large.position = {0, large.halfExtent.y, 1};
    const auto second = towers.create(engine, large);
    for (double x : {2., 3., 4.}) {
        (void)spawn(enemies, mesh, x);
    }
    combat.clear();
    for (int frame = 0; frame < 600 && !enemies.enemies().empty(); ++frame) {
        combat.update(towers, enemies, 1.0 / 60);
    }
    require(enemies.enemies().empty() && enemies.metrics().defeated == 3 && combat.shotsFired() >= 3,
            "simultaneous mixed tower combat did not defeat enemies");
    require(engine.update(), "projectile/death rendering failed");
    require(towers.remove(engine, first) && towers.remove(engine, second), "mixed tower cleanup failed");
    enemies.clear();
}

void checkFrameRates(Engine& engine, const NavMesh& mesh, TowerSystem& towers) {
    EnemyDefinition definition;
    definition.speed = 0;
    definition.maximumHealth = 10000;
    EnemySystem enemies{engine, definition};
    auto small = towers.definition("basic").instance();
    small.position = {0, small.halfExtent.y, 0};
    const auto tower = towers.create(engine, small);
    std::optional<float> expectedHealth;
    std::optional<std::uint64_t> expectedShots;
    for (const int fps : {60, 30, 5, 1}) {
        enemies.clear();
        const auto id = spawn(enemies, mesh, 2);
        CombatSystem combat{engine};
        for (int frame = 0; frame < fps * 4; ++frame) {
            combat.update(towers, enemies, 1.0 / fps);
        }
        const auto health = enemies.find(id)->health;
        require((!expectedHealth || health == *expectedHealth) &&
                    (!expectedShots || combat.shotsFired() == *expectedShots),
                "attack cadence or projectile hits changed with frame rate");
        expectedHealth = health;
        expectedShots = combat.shotsFired();
    }
    require(*expectedHealth < 10000 && *expectedShots == 6, "configured fire rate incorrect");
    require(towers.remove(engine, tower), "cadence tower cleanup failed");
}

void checkUpgrades(Engine& engine, const NavMesh& mesh, TowerSystem& towers) {
    EnemyDefinition enemyDefinition;
    enemyDefinition.speed = 0;
    enemyDefinition.maximumHealth = 10000;
    EnemySystem enemies{engine, enemyDefinition};
    for (const auto* type : {"basic", "wide"}) {
        const auto& definition = towers.definition(type);
        auto tower = definition.instance();
        tower.position = {0, tower.halfExtent.y, 0};
        const auto object = towers.create(engine, tower);
        auto other = tower;
        other.position.z = -7;
        const auto independent = towers.create(engine, other);
        const auto transform = engine.scene()->object(object).local;
        const auto revision = towers.revision();
        EconomySystem economy;
        economy.reset(1000);
        const auto target = spawn(enemies, mesh, definition.statistics().attackRange + 0.25);
        CombatSystem combat{engine};
        combat.update(towers, enemies, 0.1);
        require(combat.shotsFired() == 0, "base tower attacked beyond its range");
        towers.upgrade(object, economy);
        towers.synchronize(engine);
        require(towers.progress(object).level == 2 && towers.progress(independent).level == 1 &&
                    towers.revision() == revision &&
                    engine.scene()->object(object).local.matrix() == transform.matrix() &&
                    economy.gold() == 1000 - definition.statistics(2).upgradeCost,
                "upgrade changed another tower, footprint, transform, navigation revision or incorrect gold");
        combat.update(towers, enemies, 0.01);
        require(combat.shotsFired() == 1 &&
                    combat.projectiles().projectiles().front().damage == definition.statistics(2).damage,
                "upgrade did not change real targeting range/projectile damage");
        towers.upgrade(object, economy);
        require(combat.projectiles().projectiles().front().damage == definition.statistics(2).damage,
                "upgrade retroactively changed an in-flight projectile");
        combat.update(towers, enemies, 2);
        float inFlight = 0;
        for (const auto& projectile : combat.projectiles().projectiles()) {
            inFlight += projectile.damage;
        }
        const auto dealt = definition.statistics(2).damage +
                           static_cast<float>(combat.shotsFired() - 1) * definition.statistics(3).damage - inFlight;
        require(combat.shotsFired() >= 1 + static_cast<std::uint64_t>(definition.statistics(3).fireRate) &&
                    enemies.find(target)->health == enemyDefinition.maximumHealth - dealt && dealt > 0,
                "upgraded attack rate or actual impact damage incorrect");
        const auto gold = economy.gold();
        bool maximumRejected = false;
        try {
            towers.upgrade(object, economy);
        } catch (const std::invalid_argument&) {
            maximumRejected = true;
        }
        require(maximumRejected && economy.gold() == gold && towers.progress(object).level == 3,
                "maximum-level upgrade consumed gold or changed the level");
        engine.scene()->setEnabled(object, false);
        towers.synchronize(engine);
        engine.scene()->setEnabled(object, true);
        towers.synchronize(engine);
        require(towers.progress(object).level == 3, "scene synchronization lost a runtime upgrade");
        towers.resetUpgrades();
        require(towers.progress(object).level == 1 && towers.progress(independent).level == 1,
                "runtime level reset failed");
        require(towers.remove(engine, object) && towers.remove(engine, independent), "upgrade tower cleanup failed");
        bool removedRejected = false;
        try {
            towers.upgrade(object, economy);
        } catch (const std::invalid_argument&) {
            removedRejected = true;
        }
        require(removedRejected && economy.gold() == gold, "removed tower upgrade consumed gold");
        enemies.clear();
    }
}
} // namespace

int main() {
    try {
        Engine engine{EngineConfig{.windowTitle = "Combat runtime checks"}};
        engine.setScene(std::make_unique<Scene>("Combat fixture"));
        NavigationGeometry geometry;
        geometry.addBox({-10, -0.2F, -10}, {10, 0, 10}, true);
        NavMesh mesh;
        require(mesh.build(geometry), "combat NavMesh failed");
        TowerSystem towers;
        towers.synchronize(engine);
        const auto& small = towers.definition("basic");
        const auto& large = towers.definition("wide");
        require(large.statistics().damage > small.statistics().damage &&
                    small.statistics().attackRange > large.statistics().attackRange &&
                    large.statistics().damage * large.statistics().fireRate >
                        small.statistics().damage * small.statistics().fireRate &&
                    large.constructionCost > small.constructionCost,
                "Large Tower is not explicitly more powerful/costly");
        EnemyDefinition definition;
        definition.speed = 0;
        EnemySystem enemies{engine, definition};
        checkTowerVisuals(engine, towers, enemies, mesh);
        checkProjectiles(engine, enemies, mesh, small);
        checkTowers(engine, enemies, mesh, towers);
        checkFrameRates(engine, mesh, towers);
        checkUpgrades(engine, mesh, towers);
        std::cout << "PASS tower targeting/range/cadence, mixed towers, projectile impacts/stale IDs, health, "
                     "death animation/cleanup and 1/5/30/60 FPS combat.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL combat: " << error.what() << '\n';
        return 1;
    }
}
