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
    for (std::size_t index = 0; index < count; ++index) {
        const auto type = index == 0 && boss ? *boss : types[index % types.size()];
        if (navigation.spawnAt(engine, type, {{-15, -4}, {15, 4}, 0}, 1) != 1) {
            throw std::runtime_error("Stress fixture could not spawn its exact enemy count");
        }
    }
    verifyQueries(enemies);
    CombatSystem towers{engine};
    ProjectileSystem volley{engine};
    EnemyCombatSystem ai;
    HeroCombatSystem heroCombat;
    std::vector<double> frames;
    double aiTime = 0, movementTime = 0, heroTime = 0, combatTime = 0, preparation = 0, render = 0, gpu = 0,
           animation = 0;
    std::size_t gpuSamples = 0, peakProjectiles = 0;
    for (int frame = -30; frame < samples; ++frame) {
        double measuredAi = 0, measuredMovement = 0, measuredHeroes = 0, measuredCombat = 0;
        const auto start = Clock::now();
        if (!engine.update([&](double) {
                constexpr double step = 1.0 / 60;
                auto begin = Clock::now();
                navigation.synchronize(engine);
                ai.prepare(enemies, navigation.towers(), &heroes, {17, 0}, step, &navigation.combatGeometry());
                measuredAi = elapsed(begin);
                begin = Clock::now();
                navigation.update(engine, step);
                measuredMovement = elapsed(begin);
                begin = Clock::now();
                ai.resolve(engine, enemies, navigation.towers(), &heroes, step);
                navigation.synchronize(engine);
                measuredAi += elapsed(begin);
                begin = Clock::now();
                heroCombat.update(heroes, enemies, step, true);
                measuredHeroes = elapsed(begin);
                begin = Clock::now();
                if (frame == 0) {
                    // A fixed volley stresses simultaneous projectiles without changing tower statistics.
                    const auto active = enemies.enemies();
                    for (std::size_t shot = 0; shot < 128 && !active.empty(); ++shot) {
                        volley.spawn({0, 2, 0},
                                     active[shot % active.size()].id,
                                     navigation.towers().definition("basic"));
                    }
                }
                towers.update(navigation.towers(), enemies, step, &navigation.combatGeometry());
                volley.update(enemies, step, &navigation.combatGeometry());
                measuredCombat = elapsed(begin);
            })) {
            throw std::runtime_error("Stress window closed");
        }
        const double frameMilliseconds = elapsed(start);
        peakProjectiles =
            std::max(peakProjectiles, towers.projectiles().projectiles().size() + volley.projectiles().size());
        for (const auto& enemy : enemies.enemies()) {
            if (!enemy.navigation->contains(enemy.position)) {
                throw std::runtime_error("Stress enemy left its radius-aware navigation surface");
            }
        }
        if (frame >= 0) {
            frames.push_back(frameMilliseconds);
            aiTime += measuredAi;
            movementTime += measuredMovement;
            heroTime += measuredHeroes;
            combatTime += measuredCombat;
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
              << enemies.enemies().size() << ',' << checksum << ',' << animation / samples << std::endl;
    towers.clear();
    volley.clear();
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
    heroes.clear();
    navigation.shutdown();
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
        if (argc > 3 || (argc > 2 && !animated)) {
            throw std::invalid_argument("Usage: CombatStressChecks [samples] [--animated]");
        }
        if (samples < 1 || samples > 3600) {
            throw std::invalid_argument("Sample count must be between 1 and 3600");
        }
        std::cout << "scenario,enemies,fps,mean_ms,p50_ms,p95_ms,p99_ms,max_ms,ai_ms,movement_ms,hero_ms,combat_ms,"
                     "preparation_ms,render_ms,gpu_ms,private_mb,working_mb,peak_projectiles,impacts,defeated,alive,"
                     "checksum,animation_ms\n";
        for (const auto count : {50, 100, 200}) {
            scenario(engine, count, samples, animated);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Combat stress: " << error.what() << '\n';
        return 1;
    }
}
