#include "allocation_counter.hpp"
#include "navigation/navigation_session.hpp"
#include "systems/combat_system.hpp"
#include "systems/enemy_combat_system.hpp"
#include "systems/hero_combat_system.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

using namespace grimlock;
using namespace grimlock::game;

namespace {
using Clock = std::chrono::steady_clock;

double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void verifyQueries(const EnemySystem& enemies) {
    for (const auto position : {glm::dvec3{-12, 0, -3}, glm::dvec3{0, 0, 0}, glm::dvec3{12, 4, 3}}) {
        std::vector<EnemyId> nearby, expected;
        enemies.visitNearby(position, 5, [&](const Enemy& enemy) {
            const auto offset = enemy.position - position;
            if (enemy.active && enemy.alive && glm::dot(offset, offset) <= 25) {
                nearby.push_back(enemy.id);
            }
        });
        for (const auto& enemy : enemies.enemies()) {
            if (enemies.registry().get<EnemyRuntimeComponent>(enemy.id) != &enemy ||
                !enemies.registry().has<TeamComponent>(enemy.id)) {
                throw std::runtime_error("Packed enemy view is not backed by ECS components");
            }
            if (enemies.find(enemy.id) != &enemy) {
                throw std::runtime_error("Packed enemy index returned a stale reference");
            }
            const auto offset = enemy.position - position;
            if (enemy.active && enemy.alive && glm::dot(offset, offset) <= 25) {
                expected.push_back(enemy.id);
            }
        }
        std::sort(nearby.begin(), nearby.end());
        std::sort(expected.begin(), expected.end());
        if (nearby != expected) {
            throw std::runtime_error("Nearby query disagrees with exact full scan");
        }
    }
}

void scenario(Engine& engine, std::size_t count, int samples, bool animated) {
    auto scene = std::make_unique<Scene>("Deterministic combat stress");
    scene->setNavigationGrid(GridConfiguration{40, 32, 1, {-20, 0, -16}});
    engine.setScene(std::move(scene));
    engine.scene()->setResolvedCamera(Camera{.position = {0, 28, 26}, .target = {0, 0, 0}});
    NavigationSession navigation;
    navigation.synchronize(engine);
    for (int index = 0; index < 8; ++index) {
        auto tower = navigation.towers().definition(index % 2 ? "wide" : "basic").instance();
        tower.position = {-7.0 + (index % 4) * 4.0, tower.halfExtent.y, index < 4 ? -6.0 : 6.0};
        (void)navigation.towers().create(engine, tower);
    }
    navigation.synchronize(engine);
    navigation.setDestination(glm::dvec2{17, 0});
    HeroSystem heroes{engine};
    heroes.shareCombatGeometry(navigation.combatGeometry());
    heroes.synchronize(navigation.towers());
    for (int index = 0; index < 3; ++index) {
        (void)heroes.deploy({-4.0 + index * 4, 0, 0});
    }
    auto& enemies = *navigation.enemies();
    std::vector<EnemyTypeId> types{0};
    std::optional<EnemyTypeId> boss;
    for (std::size_t type = 1; type < enemies.typeCount(); ++type) {
        const auto& definition = enemies.definition(type);
        if (definition.boss) {
            boss = type;
        } else if ((animated && definition.name == "Black Rat") || definition.name == "Grunt" ||
                   definition.name == "Siege" || definition.name == "Hunter") {
            types.push_back(type);
        }
    }
    checks::AllocationScope spawning;
    const auto spawnStart = Clock::now();
    for (std::size_t index = 0; index < count; ++index) {
        const auto type = index == 0 && boss ? *boss : types[index % types.size()];
        const double depth = count > 200 ? 12 : 4;
        if (navigation.spawnAt(engine, type, {{-15, -depth}, {15, depth}, 0}, 1) != 1) {
            throw std::runtime_error("Stress fixture could not spawn its exact enemy count");
        }
    }
    const auto spawnMilliseconds = elapsed(spawnStart);
    const auto spawnAllocations = spawning.stop();
    verifyQueries(enemies);
    checks::AllocationScope towerSyncScope;
    const auto towerSyncStart = Clock::now();
    for (int pass = 0; pass < 1000; ++pass) {
        navigation.towers().synchronize(engine);
    }
    const auto towerSyncMilliseconds = elapsed(towerSyncStart);
    const auto towerSyncAllocations = towerSyncScope.stop();
    std::cout << "TOWER_SYNC," << count << ',' << towerSyncMilliseconds << ',' << towerSyncAllocations.count << ','
              << towerSyncAllocations.bytes << '\n';
    CombatSystem towers{engine};
    ProjectileSystem volley{engine};
    EnemyCombatSystem ai;
    HeroCombatSystem heroCombat;
    std::vector<double> frames;
    double aiTime = 0, movementTime = 0, heroTime = 0, combatTime = 0, preparation = 0, render = 0, gpu = 0,
           animation = 0;
    std::size_t gpuSamples = 0, peakProjectiles = 0;
    double worstFrame = 0, worstAi = 0, worstMovement = 0, worstHeroes = 0, worstCombat = 0;
    bool worstRebuilt = false;
    std::uint64_t frameAllocations = 0, frameBytes = 0, steadyAllocations = 0;
    std::uint64_t aiAllocations = 0, movementAllocations = 0, heroAllocations = 0, combatAllocations = 0;
    int frame = 0;
    double measuredAi = 0, measuredMovement = 0, measuredHeroes = 0, measuredCombat = 0;
    std::uint64_t measuredAiAllocations = 0, measuredMovementAllocations = 0, measuredHeroAllocations = 0,
                  measuredCombatAllocations = 0;
    const std::function<void(double)> update = [&](double) {
        constexpr double step = 1.0 / 60;
        auto begin = Clock::now();
        checks::AllocationScope aiScope;
        navigation.synchronize(engine);
        ai.prepare(enemies, navigation.towers(), &heroes, {17, 0}, step, &navigation.combatGeometry());
        measuredAi = elapsed(begin);
        measuredAiAllocations = aiScope.stop().count;
        begin = Clock::now();
        checks::AllocationScope movementScope;
        navigation.update(engine, step);
        measuredMovement = elapsed(begin);
        measuredMovementAllocations = movementScope.stop().count;
        begin = Clock::now();
        checks::AllocationScope resolveScope;
        ai.resolve(engine, enemies, navigation.towers(), &heroes, step);
        navigation.synchronize(engine);
        measuredAi += elapsed(begin);
        measuredAiAllocations += resolveScope.stop().count;
        begin = Clock::now();
        checks::AllocationScope heroScope;
        heroCombat.update(heroes, enemies, step, true);
        measuredHeroes = elapsed(begin);
        measuredHeroAllocations = heroScope.stop().count;
        begin = Clock::now();
        checks::AllocationScope combatScope;
        if (frame == 0) {
            // A fixed volley stresses simultaneous projectiles without changing tower statistics.
            const auto active = enemies.enemies();
            for (std::size_t shot = 0; shot < 128 && !active.empty(); ++shot) {
                volley.spawn({0, 2, 0}, active[shot % active.size()].id, navigation.towers().definition("basic"));
            }
        }
        towers.update(navigation.towers(), enemies, step, &navigation.combatGeometry());
        volley.update(enemies, step, &navigation.combatGeometry());
        measuredCombat = elapsed(begin);
        measuredCombatAllocations = combatScope.stop().count;
    };
    for (frame = -30; frame < samples; ++frame) {
        measuredAi = measuredMovement = measuredHeroes = measuredCombat = 0;
        measuredAiAllocations = measuredMovementAllocations = measuredHeroAllocations = measuredCombatAllocations = 0;
        const auto revision = navigation.revision();
        const auto start = Clock::now();
        checks::AllocationScope allocationScope;
        if (!engine.update(update)) {
            throw std::runtime_error("Stress window closed");
        }
        const double frameMilliseconds = elapsed(start);
        const auto allocations = allocationScope.stop();
        peakProjectiles =
            std::max(peakProjectiles, towers.projectiles().projectiles().size() + volley.projectiles().size());
        for (const auto& enemy : enemies.enemies()) {
            if (!enemy.navigation->contains(enemy.position)) {
                throw std::runtime_error("Stress enemy left its radius-aware navigation surface");
            }
        }
        if (frame >= 0) {
            if (frameMilliseconds > worstFrame) {
                worstFrame = frameMilliseconds;
                worstAi = measuredAi;
                worstMovement = measuredMovement;
                worstHeroes = measuredHeroes;
                worstCombat = measuredCombat;
                worstRebuilt = navigation.revision() != revision;
            }
            frames.push_back(frameMilliseconds);
            aiTime += measuredAi;
            movementTime += measuredMovement;
            heroTime += measuredHeroes;
            combatTime += measuredCombat;
            frameAllocations += allocations.count;
            frameBytes += allocations.bytes;
            if (frame > 30) {
                steadyAllocations += allocations.count;
            }
            aiAllocations += measuredAiAllocations;
            movementAllocations += measuredMovementAllocations;
            heroAllocations += measuredHeroAllocations;
            combatAllocations += measuredCombatAllocations;
            const auto& performance = engine.performance();
            preparation += performance.preparationMilliseconds;
            animation += performance.animationMilliseconds;
            render += performance.renderMilliseconds;
            if (performance.gpuMilliseconds) {
                gpu += *performance.gpuMilliseconds;
                ++gpuSamples;
            }
        }
    }
    const auto mean = std::accumulate(frames.begin(), frames.end(), 0.0) / samples;
    std::sort(frames.begin(), frames.end());
    const auto percentile = [&](double fraction) {
        return frames[static_cast<std::size_t>((frames.size() - 1) * fraction)];
    };
    double privateMb = 0, workingMb = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                             sizeof(memory))) {
        privateMb = memory.PrivateUsage / (1024.0 * 1024);
        workingMb = memory.WorkingSetSize / (1024.0 * 1024);
    }
#endif
    double checksum = 0;
    for (const auto& enemy : enemies.enemies()) {
        checksum += enemy.id * (enemy.position.x + enemy.position.z + enemy.health);
    }
    std::cout << std::fixed << std::setprecision(3) << "STRESS," << count << ',' << 1000 / mean << ',' << mean << ','
              << percentile(0.5) << ',' << percentile(0.95) << ',' << percentile(0.99) << ',' << frames.back() << ','
              << aiTime / samples << ',' << movementTime / samples << ',' << heroTime / samples << ','
              << combatTime / samples << ',' << preparation / samples << ',' << render / samples << ','
              << (gpuSamples ? gpu / gpuSamples : -1) << ',' << privateMb << ',' << workingMb << ',' << peakProjectiles
              << ',' << towers.projectiles().impacts() + volley.impacts() << ',' << enemies.metrics().defeated << ','
              << enemies.enemies().size() << ',' << checksum << ',' << animation / samples << ','
              << enemies.registry().capacityBytes() + towers.projectiles().registry().capacityBytes() +
                     volley.registry().capacityBytes() + heroes.registry().capacityBytes() +
                     navigation.towers().registry().capacityBytes()
              << ',' << frameAllocations << ',' << frameBytes << ',' << steadyAllocations << ',' << aiAllocations << ','
              << movementAllocations << ',' << heroAllocations << ',' << combatAllocations << std::endl;
    std::cout << "WORST," << count << ',' << worstFrame << ',' << worstAi << ',' << worstMovement << ',' << worstHeroes
              << ',' << worstCombat << ',' << worstRebuilt << '\n';
    towers.clear();
    volley.clear();
    if (!enemies.enemies().empty()) {
        const auto active = enemies.enemies();
        for (std::size_t shot = 0; shot < 128; ++shot) {
            volley.spawn({0, 5, 12}, active[shot % active.size()].id, navigation.towers().definition("basic"));
        }
        checks::AllocationScope projectileScope;
        const auto projectileStart = Clock::now();
        for (int pass = 0; pass < 32; ++pass) {
            volley.update(enemies, 1.0 / 60, &navigation.combatGeometry());
        }
        const auto projectileMilliseconds = elapsed(projectileStart) / 32;
        const auto projectileAllocations = projectileScope.stop();
        std::cout << "PROJECTILES," << count << ',' << projectileMilliseconds << ',' << projectileAllocations.count
                  << ',' << projectileAllocations.bytes << '\n';
        volley.clear();
    }
    verifyQueries(enemies);
    if (enemies.enemies().size() >= 2) {
        const auto removed = enemies.enemies().front().id;
        const auto defeated = enemies.enemies().back().id;
        if (!enemies.remove(removed) || enemies.find(removed)) {
            throw std::runtime_error("Removed ID remained valid");
        }
        verifyQueries(enemies);
        if (!enemies.applyDamage(defeated, 100000) || enemies.find(defeated)) {
            throw std::runtime_error("Defeated ID remained valid");
        }
        verifyQueries(enemies);
        if (navigation.spawnAt(engine, 0, {{-15, -4}, {15, 4}, 0}, 1) != 1 || enemies.find(removed) ||
            enemies.find(defeated)) {
            throw std::runtime_error("Respawn reused a destroyed enemy ID");
        }
        verifyQueries(enemies);
    }
    const auto cleanupStart = Clock::now();
    checks::AllocationScope cleanupScope;
    heroes.clear();
    navigation.shutdown();
    const auto cleanupMilliseconds = elapsed(cleanupStart);
    const auto cleanup = cleanupScope.stop();
    std::cout << "LIFECYCLE," << count << ',' << spawnMilliseconds << ',' << spawnAllocations.count << ','
              << spawnAllocations.bytes << ',' << cleanupMilliseconds << ',' << cleanup.count << ',' << cleanup.bytes
              << '\n';
    if (!engine.update() || engine.performance().instances != 0) {
        throw std::runtime_error("Battle cleanup retained runtime render instances");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        Engine engine{EngineConfig{.windowTitle = "Combat stress", .width = 960, .height = 540}};
        engine.setPerformanceEnabled(true);
        const int samples = argc > 1 ? std::stoi(argv[1]) : 180;
        const bool animated = argc > 2 && std::string_view{argv[2]} == "--animated";
        const bool large = argc > 2 && std::string_view{argv[2]} == "--large";
        if (argc > 3 || (argc > 2 && !animated && !large)) {
            throw std::invalid_argument("Usage: CombatStressChecks [samples] [--animated|--large]");
        }
        if (samples < 1 || samples > 3600) {
            throw std::invalid_argument("Sample count must be between 1 and 3600");
        }
        std::cout << "scenario,enemies,fps,mean_ms,p50_ms,p95_ms,p99_ms,max_ms,ai_ms,movement_ms,hero_ms,combat_ms,"
                     "preparation_ms,render_ms,gpu_ms,private_mb,working_mb,peak_projectiles,impacts,defeated,alive,"
                     "checksum,animation_ms,ecs_capacity_bytes,frame_new,frame_new_bytes,steady_new,ai_new,movement_"
                     "new,hero_new,combat_new\n";
        for (const auto count : {50, 100, 200}) {
            scenario(engine, count, samples, animated);
        }
        if (large) {
            scenario(engine, 500, samples, false);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Combat stress: " << error.what() << '\n';
        return 1;
    }
}
