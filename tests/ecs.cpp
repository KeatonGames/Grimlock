#include <chrono>
#include <grimlock/ecs.hpp>
#include <iostream>
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
} // namespace

int main() {
    try {
        lifetime();
        iteration();
        benchmark();
        std::cout << "PASS ECS generations, sparse components, joins, RAII, nested/exception-safe deferred deletion.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL ECS: " << error.what() << '\n';
        return 1;
    }
}
