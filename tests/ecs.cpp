#include "allocation_counter.hpp"

#include <chrono>
#include <grimlock/ecs.hpp>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>

using namespace grimlock::ecs;

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Position {
    double x = 0, y = 0;
};

static_assert(!std::is_copy_constructible_v<Storage<Position>> && !std::is_copy_assignable_v<Storage<Position>>);
static_assert(!std::is_move_constructible_v<Storage<Position>> && !std::is_move_assignable_v<Storage<Position>>);

struct Health {
    int value = 100;
};

struct Resource {
    std::shared_ptr<int> value;
};

struct Owned {
    std::unique_ptr<int> value;

    explicit Owned(int initial) : value(std::make_unique<int>(initial)) {
        if (initial < 0) {
            throw std::invalid_argument("constructor failure fixture");
        }
    }

    Owned(Owned&&) noexcept = default;
    Owned& operator=(Owned&&) noexcept = default;
};

void lifetime() {
    Registry registry;
    require(!registry.valid(nullEntity) && !registry.destroy(nullEntity), "null entity was valid");
    const auto first = registry.create(), second = registry.create();
    registry.emplace<Position>(first, Position{1, 2});
    registry.emplace<Health>(first, Health{50});
    registry.emplace<Position>(second, Position{3, 4});
    require(registry.has<Health>(first) && !registry.has<Health>(second), "component lookup failed");
    int visited = 0;
    registry.each<Position, Health>([&](Entity entity, Position& position, Health& health) {
        require(entity == first && position.x == 1 && health.value == 50, "component join failed");
        ++visited;
    });
    require(visited == 1, "join visited a missing component");
    registry.sort<Position>([](const Position& first, const Position& second) {
        return first.x > second.x;
    });
    require(registry.entities<Position>().front() == second && registry.get<Position>(first)->x == 1 &&
                registry.get<Position>(second)->x == 3,
            "packed sorting changed component identity");
    registry.sort<Position>([](const Position& first, const Position& second) {
        return first.x < second.x;
    });
    require(registry.destroy(first) && !registry.destroy(first) && !registry.get<Position>(first),
            "destroy left a stale handle");
    require(registry.get<Position>(second)->x == 3, "packed erase lost the moved entity");
    const auto reused = registry.create();
    require(entityIndex(reused) == entityIndex(first) && entityGeneration(reused) != entityGeneration(first),
            "slot reuse lost its generation");
    registry.emplace<Owned>(reused, 7);
    require(!registry.get<Owned>(first) && *registry.get<Owned>(reused)->value == 7,
            "stale handle accessed a recycled component");
    const Registry& read = registry;
    require(read.get<Position>(second)->y == 4 && read.has<Owned>(reused), "const component lookup failed");
    auto resource = std::make_shared<int>(7);
    registry.emplace<Resource>(reused, Resource{resource});
    require(resource.use_count() == 2, "resource fixture did not enter the component pool");
    registry.clear();
    require(resource.use_count() == 1, "clear failed to destroy an owned component resource");
    require(registry.size() == 0 && !registry.valid(second) && !registry.valid(reused), "clear left live entities");
    const auto next = registry.create();
    require(!registry.valid(first) && !registry.valid(second) && !registry.valid(reused) && registry.valid(next),
            "clear reset generations");
    bool rejected = false;
    try {
        registry.emplace<Health>(first);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "component insertion accepted a stale handle");
    rejected = false;
    try {
        registry.emplace<Owned>(next, -1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && !registry.has<Owned>(next) && registry.storage<Owned>().empty(),
            "throwing component constructor corrupted its pool");
    registry.emplace<Health>(next);
    rejected = false;
    try {
        registry.emplace<Health>(next);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && registry.get<Health>(next)->value == 100, "duplicate insertion replaced authoritative state");
    require(registry.remove<Health>(next) && !registry.remove<Health>(next), "component removal failed");
}

void iteration() {
    Registry registry;
    registry.reserveEntities(500);
    registry.reserve<Position>(500);
    registry.reserve<Health>(500);
    std::vector<Entity> handles;
    for (int index = 0; index < 500; ++index) {
        const auto entity = registry.create();
        handles.push_back(entity);
        registry.emplace<Position>(entity, Position{static_cast<double>(index), 0});
        registry.emplace<Health>(entity);
    }
    int visits = 0;
    registry.each<Position, Health>([&](Entity entity, Position& position, Health&) {
        ++visits;
        if (static_cast<int>(position.x) % 2 == 0) {
            require(registry.destroy(entity) && !registry.valid(entity),
                    "deferred destruction did not invalidate its handle");
        } else {
            require(registry.remove<Health>(entity), "deferred component removal failed");
        }
        position.y = 9; // References remain valid until this view ends, even after scheduling destruction.
    });
    require(visits == 500 && registry.size() == 250 && registry.storage<Position>().size() == 250 &&
                registry.storage<Health>().empty(),
            "structural mutation invalidated iteration");
    registry.each<Position>([&](Entity entity, Position& position) {
        require(position.y == 9 && registry.valid(entity), "deferred compaction corrupted a survivor");
        registry.each<Position>([&](Entity nested, Position&) {
            if (nested == entity) {
                registry.destroy(nested);
            }
        });
    });
    require(registry.size() == 0 && registry.storage<Position>().empty(), "nested views did not flush removals");
    auto entity = registry.create();
    registry.emplace<Health>(entity);
    bool threw = false;
    try {
        registry.each<Health>([&](Entity current, Health&) {
            registry.destroy(current);
            throw std::runtime_error("visitor failure fixture");
        });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw && registry.size() == 0 && registry.storage<Health>().empty(),
            "exception left deferred commands uncommitted");
    entity = registry.create();
    registry.emplace<Health>(entity);
    registry.each<Health>([&](Entity current, Health&) {
        bool rejected = false;
        try {
            registry.emplace<Position>(current);
        } catch (const std::logic_error&) {
            rejected = true;
        }
        require(rejected, "view allowed component insertion to invalidate references");
    });
    const auto skipped = registry.create(), removed = registry.create();
    registry.emplace<Health>(skipped);
    registry.emplace<Health>(removed);
    visits = 0;
    registry.each<Health>([&](Entity current, Health&) {
        ++visits;
        if (current == entity) {
            registry.destroy(skipped);
            registry.remove<Health>(removed);
        }
    });
    require(visits == 2 && !registry.valid(skipped) && registry.valid(removed) && !registry.has<Health>(removed),
            "views did not respect immediate invalidation and deferred component removal");
}

void benchmark() {
    Registry registry;
    registry.reserveEntities(500);
    registry.reserve<Position>(500);
    registry.reserve<Health>(500);
    const auto start = std::chrono::steady_clock::now();
    for (int cycle = 0; cycle < 200; ++cycle) {
        for (int index = 0; index < 500; ++index) {
            const auto entity = registry.create();
            registry.emplace<Position>(entity);
            registry.emplace<Health>(entity);
        }
        registry.each<Position, Health>([&](Entity entity, Position& position, Health& health) {
            position.x += health.value;
            registry.destroy(entity);
        });
    }
    require(registry.size() == 0, "benchmark leaked live entities");
    std::cout << "100000 create/join/update/deferred-destroy operations: "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
              << " ms; retained vector capacity " << registry.capacityBytes() << " bytes\n";
}

void accessBenchmark() {
    Registry registry;
    registry.reserveEntities(500);
    registry.reserve<Position>(500);
    registry.reserve<Health>(500);
    std::vector<Entity> handles;
    for (int index = 0; index < 500; ++index) {
        const auto entity = registry.create();
        handles.push_back(entity);
        registry.emplace<Position>(entity);
        registry.emplace<Health>(entity);
    }
    grimlock::checks::AllocationScope allocations;
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t sum = 0;
    for (int pass = 0; pass < 2000; ++pass) {
        for (const auto entity : handles) {
            sum += registry.get<Health>(entity)->value;
        }
    }
    const auto lookup = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto& healthPool = registry.storage<Health>();
    const auto cachedStart = std::chrono::steady_clock::now();
    std::uint64_t cachedSum = 0;
    for (int pass = 0; pass < 2000; ++pass) {
        for (const auto entity : handles) {
            cachedSum += healthPool.get(entity)->value;
        }
    }
    const auto cached =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cachedStart).count();
    const auto iterationStart = std::chrono::steady_clock::now();
    for (int pass = 0; pass < 2000; ++pass) {
        registry.each<Position, Health>([](Entity, Position& position, Health& health) {
            position.x += health.value;
        });
    }
    const auto iteration =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - iterationStart).count();
    const auto sample = allocations.stop();
    require(sum == 100000000 && cachedSum == sum && registry.get<Position>(handles.front())->x == 200000,
            "ECS access benchmark checksum failed");
    require(sample.count == 0, "reserved ECS lookup/iteration allocated during updates");
    std::cout << "ECS_ACCESS,lookup_1m_ms=" << lookup << ",cached_1m_ms=" << cached << ",join_1m_ms=" << iteration
              << ",new=" << sample.count << '\n';
}

void deferredAllocationFailure() {
    Registry registry;
    const auto entity = registry.create();
    registry.emplace<Position>(entity, Position{7, 0});
    bool rejected = false;
    try {
        grimlock::checks::AllocationFailure failure{0};
        registry.each<Position>([&](Entity current, Position&) {
            registry.destroy(current);
        });
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    require(rejected && registry.valid(entity) && registry.get<Position>(entity)->x == 7,
            "failed deferred destruction invalidated a live entity");
    rejected = false;
    try {
        grimlock::checks::AllocationFailure failure{0};
        registry.each<Position>([&](Entity current, Position&) {
            registry.remove<Position>(current);
        });
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    require(rejected && registry.has<Position>(entity), "failed deferred removal lost its component");
    registry.each<Position>([&](Entity current, Position&) {
        registry.destroy(current);
    });
    require(registry.size() == 0 && registry.storage<Position>().empty(),
            "view failed to recover after queue allocation failure");
}

std::uint64_t randomizedHistory(std::uint32_t seed) {
    Registry registry;

    struct Expected {
        bool position = false;
        double x = 0;
        int health = 100;
    };

    std::map<Entity, Expected> expected;
    std::vector<Entity> history;
    const auto random = [&]() {
        seed = seed * 1664525U + 1013904223U;
        return seed;
    };
    for (int step = 0; step < 20000; ++step) {
        const auto value = random();
        const auto operation = (value >> 24) % 7;
        if (history.empty() || (operation == 0 && expected.size() < 128)) {
            const auto entity = registry.create();
            history.push_back(entity);
            expected.emplace(entity, Expected{});
            registry.emplace<Health>(entity);
        } else {
            const auto entity = history[value % history.size()];
            const auto found = expected.find(entity);
            if (operation == 1) {
                require(registry.destroy(entity) == (found != expected.end()),
                        "history destroy disagreed with reference state");
                expected.erase(entity);
            } else if (operation == 2) {
                require(registry.valid(entity) == (found != expected.end()), "history resurrected a stale generation");
                const auto* health = registry.get<Health>(entity);
                require((health != nullptr) == (found != expected.end()),
                        "history returned a missing health component");
                if (health) {
                    require(health->value == found->second.health, "history health data diverged");
                }
            } else if (operation == 3 && found != expected.end() && !found->second.position) {
                registry.emplace<Position>(entity, Position{static_cast<double>(step), 0});
                found->second.position = true;
                found->second.x = step;
            } else if (operation == 4) {
                const bool present = found != expected.end() && found->second.position;
                require(registry.remove<Position>(entity) == present, "history component removal diverged");
                if (present) {
                    found->second.position = false;
                }
            } else if (operation == 5) {
                registry.each<Position, Health>([&](Entity current, Position& position, Health& health) {
                    auto& reference = expected.at(current);
                    require(reference.position && reference.x == position.x && reference.health == health.value,
                            "history join read mismatched components");
                    position.x += health.value;
                    reference.x = position.x;
                    ++health.value;
                    ++reference.health;
                    if (health.value % 13 == 0) {
                        registry.destroy(current);
                        expected.erase(current);
                    }
                });
            } else if (operation == 6 && step % 97 == 0) {
                registry.clear();
                expected.clear();
            }
        }
        if (step % 113 == 0) {
            require(registry.size() == expected.size(), "history leaked or lost an entity");
            for (const auto& [entity, reference] : expected) {
                require(registry.valid(entity) && registry.get<Health>(entity)->value == reference.health,
                        "history lookup failed after compaction");
                const auto* position = registry.get<Position>(entity);
                require((position != nullptr) == reference.position && (!position || position->x == reference.x),
                        "history sparse position index diverged");
            }
            registry.sort<Position>([](const Position& first, const Position& second) {
                return first.x < second.x;
            });
        }
    }
    std::uint64_t checksum = 0;
    for (const auto& [entity, reference] : expected) {
        checksum ^= entity * 1099511628211ULL + reference.health + static_cast<std::uint64_t>(reference.x);
    }
    registry.clear();
    require(registry.storage<Position>().empty() && registry.storage<Health>().empty(),
            "history cleanup left components");
    return checksum;
}
} // namespace

int main() {
    try {
        lifetime();
        iteration();
        deferredAllocationFailure();
        require(randomizedHistory(0x7137U) == randomizedHistory(0x7137U),
                "identical ECS operation histories were not deterministic");
        benchmark();
        accessBenchmark();
        std::cout << "PASS ECS generations, sparse components, joins, RAII, nested/exception-safe deferred deletion.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL ECS: " << error.what() << '\n';
        return 1;
    }
}
