#include "runtime/level_runtime.hpp"

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

enum class Strategy {
    mixed,
    small,
    empty,
    rear,
    neglect
};

struct Result {
    LevelPhase phase;
    std::uint64_t health, gold, defeated, arrived;
    std::size_t waves;
};

Result play(Engine& engine, Strategy strategy) {
    LevelRuntime runtime;
    runtime.load(engine, "assets/levels/first_watch.json");
    auto& session = runtime.level();
    auto& navigation = runtime.navigation();
    const auto build = [&](const char* type, glm::dvec2 position) {
        auto tower = navigation.towers().definition(type).instance();
        tower.position = {position.x, tower.halfExtent.y, position.y};
        require(runtime.heroes()->towerError(tower).empty(), "playable plan overlaps a hero");
        return session.buildTower(engine, navigation, tower);
    };
    ObjectId first = 0, second = 0, third = 0, fourth = 0;
    const auto hero = runtime.heroes()->heroes().front().id;
    if (strategy == Strategy::mixed || strategy == Strategy::neglect) {
        first = build("basic", {-3.3, 0});
        second = build("wide", {5, 0});
        runtime.heroes()->synchronize(navigation.towers());
        (void)runtime.heroes()->deploy({-0.3, 0, 1.5}, hero);
    } else if (strategy == Strategy::small) {
        first = build("basic", {-3.3, 0});
        second = build("basic", {0.1, -2.5});
        third = build("basic", {0.1, 2.5});
        runtime.heroes()->synchronize(navigation.towers());
        (void)runtime.heroes()->deploy({-0.3, 0, 0}, hero);
    } else if (strategy == Strategy::rear) {
        (void)runtime.heroes()->deploy({4, 0, 0}, hero);
        first = build("basic", {4, -2.5});
        second = build("wide", {4, 2.5});
    }
    const auto upgrade = [&](ObjectId tower) {
        if (navigation.towers().contains(tower) && session.upgradeError(navigation, tower).empty()) {
            session.upgradeTower(engine, navigation, tower);
        }
    };
    HeroCombatSystem abilities;
    while (session.waves()->phase() == LevelPhase::preparation) {
        const auto wave = session.waves()->wave();
        if (strategy != Strategy::empty && strategy != Strategy::neglect && wave > 0) {
            if (strategy == Strategy::small) {
                if (wave >= 3 && !fourth && session.economy().gold() >= 30) {
                    fourth = build("basic", {3.7, 0});
                }
                upgrade(first);
                upgrade(second);
                upgrade(third);
            } else if (strategy == Strategy::mixed) {
                if (wave == 1) {
                    upgrade(first);
                }
                if (wave == 3 || wave == 4) {
                    upgrade(second);
                }
                if (wave >= 2 && !third &&
                    session.economy().gold() >= navigation.towers().definition("basic").constructionCost) {
                    third = build("basic", {3.7, 0});
                }
            } else {
                upgrade(first);
                upgrade(second);
            }
            if (((wave == 2 && strategy == Strategy::mixed) || (wave == 3 && strategy == Strategy::small)) &&
                runtime.heroes()->find(hero)->health > 0) {
                runtime.heroes()->synchronize(navigation.towers());
                (void)runtime.heroes()->deploy({4, 0, -1.3}, hero);
            }
        }
        const auto goldBefore = session.economy().gold();
        runtime.startWave();
        double time = 0;
        while (session.waves()->phase() == LevelPhase::battle && time < 180) {
            if (strategy == Strategy::mixed || strategy == Strategy::small) {
                const auto* defender = runtime.heroes()->find(hero);
                std::size_t nearby = 0;
                for (const auto& enemy : navigation.enemies()->enemies()) {
                    if (glm::length(enemy.position - defender->position) <
                        runtime.heroes()->definition().combat.cleave.radius) {
                        ++nearby;
                    }
                }
                if (nearby >= 2) {
                    (void)abilities.activate(*runtime.heroes(), hero, HeroAbility::cleave, true);
                }
            }
            runtime.update(engine, 1.0 / 30);
            time += 1.0 / 30;
            for (const auto& enemy : navigation.enemies()->enemies()) {
                require(enemy.navigation->contains(enemy.position), "playable enemy left navigation");
            }
        }
        std::cout << "Strategy " << static_cast<int>(strategy) << " wave " << wave + 1 << ": "
                  << levelPhaseName(session.waves()->phase()) << "; " << time << "s; objective "
                  << session.waves()->health() << "; gold " << session.economy().gold() << "; towers "
                  << navigation.towers().towers().size() << "; hero " << runtime.heroes()->find(hero)->health
                  << "; enemies " << navigation.enemies()->enemies().size() << '\n';
        if (session.waves()->phase() == LevelPhase::battle) {
            for (const auto& enemy : navigation.enemies()->enemies()) {
                std::cout << "Stalled " << enemy.id << " type " << enemy.type << " at " << enemy.position.x << ','
                          << enemy.position.z << " state " << enemyStateName(enemy.state) << " target "
                          << static_cast<int>(enemy.combat.target.kind) << ':' << enemy.combat.target.id
                          << " remaining " << enemy.remainingDistance << " path " << enemy.path.valid << '\n';
            }
        }
        require(session.waves()->phase() != LevelPhase::battle, "playable battle stalled for 180 seconds");
        const auto reward =
            session.waves()->phase() == LevelPhase::defeat ? 0 : session.waves()->definition().waves[wave].goldReward;
        require(session.economy().gold() == goldBefore + reward, "playable wave reward incorrect");
        runtime.update(engine, 1);
        require(session.economy().gold() == goldBefore + reward, "playable reward repeated");
        require(engine.update(), "playable battle render failed");
    }
    const auto* waves = session.waves();
    const auto metrics = navigation.enemies()->metrics();
    Result result{waves->phase(),
                  waves->health(),
                  session.economy().gold(),
                  metrics.defeated,
                  metrics.arrived,
                  waves->wave()};
    runtime.restart(engine);
    require(session.waves()->phase() == LevelPhase::preparation && session.waves()->wave() == 0 &&
                session.waves()->health() == session.waves()->definition().objectiveHealth &&
                session.economy().gold() == session.waves()->definition().startingGold &&
                navigation.towers().towers().empty() && navigation.enemies()->enemies().empty() &&
                runtime.heroes()->heroes().front().health == runtime.heroes()->definition().maximumHealth,
            "playable restart did not restore initial state");
    return result;
}
} // namespace

int main() {
    try {
        Engine engine{EngineConfig{.windowTitle = "First Watch playthrough checks"}};
        for (const auto strategy :
             {Strategy::mixed, Strategy::small, Strategy::empty, Strategy::rear, Strategy::neglect}) {
            const auto result = play(engine, strategy);
            std::cout << "Result " << static_cast<int>(strategy) << ": " << levelPhaseName(result.phase) << "; kills "
                      << result.defeated << "; arrivals " << result.arrived << '\n';
            const bool poor = strategy == Strategy::empty || strategy == Strategy::neglect;
            require(result.phase == (poor ? LevelPhase::defeat : LevelPhase::victory),
                    "authored level no longer distinguishes successful and poor defensive plans");
            if (!poor) {
                require(result.waves == 5 && result.health > 0 && result.defeated + result.arrived == 90,
                        "playable victory lost scheduled enemies or skipped a wave");
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Playable: " << error.what() << '\n';
        return 1;
    }
}
