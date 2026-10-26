#include "systems/wave_system.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace grimlock::game;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <class F> void rejects(F operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "invalid configuration or transition accepted");
}

void checkSchedule(LevelDefinition definition) {
    definition.objectiveHealth = 100;
    definition.waves = {{"Exact groups", {{"Chicken", 0, 5, 2, 1, 2}, {"Black Rat", 1, 2, 2, 1, 0}}},
                        {"Last wave", {{"Bunny", 0, 1, 1, 0, 0}}}};
    WaveSystem waves{definition};
    std::size_t chickens = 0, rats = 0;
    const auto spawn = [&](const WaveGroup& group, std::size_t count) {
        (group.enemy == "Chicken" ? chickens : rats) += count;
        return count;
    };
    waves.update(50, 0, 0, spawn);
    require(chickens == 0 && waves.phase() == LevelPhase::preparation, "preparation spawned enemies");
    waves.startWave();
    rejects([&] {
        waves.startWave();
    });
    waves.update(0.5, 0, 0, spawn);
    require(chickens == 0 && rats == 0, "group spawned before authored time");
    waves.update(0.5, 0, 0, spawn);
    require(chickens == 2 && rats == 2 && waves.pending() == 3, "simultaneous groups incorrect");
    waves.update(1.9, 4, 0, spawn);
    require(chickens == 2, "interval fired early");
    waves.update(0.1, 4, 1, spawn);
    require(chickens == 4 && waves.health() == 99, "interval or objective damage incorrect");
    waves.update(20, 5, 0, spawn);
    require(chickens == 5 && !waves.pending() && waves.phase() == LevelPhase::battle,
            "exact count or active drain incorrect");
    waves.update(0, 0, 0, spawn);
    require(waves.phase() == LevelPhase::preparation && waves.wave() == 1, "next preparation not entered");
    waves.startWave();
    waves.update(0, 0, 0, spawn);
    require(waves.phase() == LevelPhase::battle, "newly spawned enemy completed wave immediately");
    waves.update(1, 0, 1, spawn);
    require(waves.phase() == LevelPhase::victory && waves.health() == 98, "final surviving wave did not win");
    waves.restart();
    require(waves.phase() == LevelPhase::preparation && waves.wave() == 0 && waves.health() == 100,
            "restart retained progress");

    waves.startWave();
    std::size_t accepted = 0;
    waves.update(10, 0, 0, [&](const WaveGroup&, std::size_t) {
        ++accepted;
        return std::size_t{1};
    });
    require(accepted == 2 && waves.pending() == 5, "partial spawns dropped scheduled enemies");
    waves.update(0, 2, 0, [](const WaveGroup&, std::size_t) {
        return std::size_t{0};
    });
    require(waves.pending() == 5, "occupied spawn silently discarded counts");
    waves.update(0, 0, 0, spawn);
    require(!waves.pending(), "pending batch could not resume");
    waves.update(0, 0, std::numeric_limits<std::uint64_t>::max(), spawn);
    require(waves.phase() == LevelPhase::defeat && waves.health() == 0, "lethal arrivals did not stop level safely");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected level path");
        auto definition = loadLevelDefinition(argv[1]);
        require(definition.waves.size() == 5 && definition.spawns.size() == 2 && definition.objectiveHealth == 10,
                "authored level incorrect");
        checkSchedule(definition);
        definition.waves[0].groups[0].count = 1001;
        rejects([&] {
            validateLevelDefinition(definition);
        });
        definition = loadLevelDefinition(argv[1]);
        definition.waves[0].groups[0].start = std::numeric_limits<double>::quiet_NaN();
        rejects([&] {
            validateLevelDefinition(definition);
        });
        definition = loadLevelDefinition(argv[1]);
        definition.scene = "../outside.json";
        rejects([&] {
            validateLevelDefinition(definition);
        });
        definition = loadLevelDefinition(argv[1]);
        definition.spawns[1].name = definition.spawns[0].name;
        rejects([&] {
            validateLevelDefinition(definition);
        });
        std::cout
            << "PASS handcrafted schedule, exact batches, retries, phases, damage, victory, defeat and validation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL waves: " << error.what() << '\n';
        return 1;
    }
}
