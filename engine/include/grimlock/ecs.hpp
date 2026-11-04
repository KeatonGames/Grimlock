#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace grimlock::ecs {

// Registry-scoped handle: low 32 bits contain index + 1; high bits contain the generation. Zero is null.
using Entity = std::uint64_t;
inline constexpr Entity nullEntity = 0;

constexpr std::uint32_t entityIndex(Entity entity) noexcept {
    return static_cast<std::uint32_t>(entity) - 1;
}

constexpr std::uint32_t entityGeneration(Entity entity) noexcept {
    return static_cast<std::uint32_t>(entity >> 32);
}

class Registry;

namespace detail {
class ComponentPool {
public:
    virtual ~ComponentPool() = default;
    [[nodiscard]] virtual std::span<const Entity> entities() const noexcept = 0;
    [[nodiscard]] virtual std::size_t capacityBytes() const noexcept = 0;

private:
    friend class ::grimlock::ecs::Registry;
    virtual void erase(Entity entity) noexcept = 0;
};
} // namespace detail

template <class T> class Storage final : public detail::ComponentPool {
    static_assert(std::is_same_v<T, std::remove_cvref_t<T>>,
                  "Component types must be unqualified; use a const registry for read-only access.");
    static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T> &&
                      std::is_nothrow_destructible_v<T>,
                  "Packed components must support nonthrowing relocation and destruction.");

public:
    Storage() = default;
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    Storage(Storage&&) = delete;
    Storage& operator=(Storage&&) = delete;

    [[nodiscard]] std::span<T> values() noexcept {
        return values_;
    }

    [[nodiscard]] std::span<const T> values() const noexcept {
        return values_;
    }

    [[nodiscard]] std::span<const Entity> entities() const noexcept override {
        return entities_;
    }

    [[nodiscard]] std::size_t capacityBytes() const noexcept override {
        return values_.capacity() * sizeof(T) + entities_.capacity() * sizeof(Entity) +
               sparse_.capacity() * sizeof(std::size_t);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return values_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return values_.empty();
    }

    [[nodiscard]] T* data() noexcept {
        return values_.data();
    }

    [[nodiscard]] const T* data() const noexcept {
        return values_.data();
    }

    auto begin() noexcept {
        return values_.begin();
    }

    auto end() noexcept {
        return values_.end();
    }

    auto begin() const noexcept {
        return values_.begin();
    }

    auto end() const noexcept {
        return values_.end();
    }

    T& operator[](std::size_t index) noexcept {
        return values_[index];
    }

    const T& operator[](std::size_t index) const noexcept {
        return values_[index];
    }

    T& back() noexcept {
        return values_.back();
    }

    const T& back() const noexcept {
        return values_.back();
    }

    operator std::span<T>() noexcept {
        return values_;
    }

    operator std::span<const T>() const noexcept {
        return values_;
    }

    [[nodiscard]] T* get(Entity entity) noexcept {
        const auto index = entityIndex(entity);
        if (index >= sparse_.size() || sparse_[index] == absent || entities_[sparse_[index]] != entity) {
            return nullptr;
        }
        return &values_[sparse_[index]];
    }

    [[nodiscard]] const T* get(Entity entity) const noexcept {
        return const_cast<Storage*>(this)->get(entity);
    }

private:
    friend class Registry;
    static constexpr auto absent = std::numeric_limits<std::size_t>::max();

    void reserve(std::size_t count) {
        if (count > values_.capacity()) {
            const auto current = values_.capacity();
            const auto growth = current <= std::numeric_limits<std::size_t>::max() / 2 ? current * 2 : count;
            values_.reserve(std::max({count, growth, std::size_t{8}}));
        }
        entities_.reserve(values_.capacity());
        sparse_.reserve(values_.capacity());
    }

    template <class... Args> T& emplace(Entity entity, Args&&... args) {
        if (get(entity)) {
            throw std::invalid_argument("Entity already has this component.");
        }
        const auto index = entityIndex(entity);
        if (index >= sparse_.size()) {
            sparse_.resize(static_cast<std::size_t>(index) + 1, absent);
        }
        entities_.push_back(entity);
        try {
            values_.emplace_back(std::forward<Args>(args)...);
        } catch (...) {
            entities_.pop_back();
            throw;
        }
        sparse_[index] = values_.size() - 1;
        return values_.back();
    }

    void erase(Entity entity) noexcept override {
        if (!get(entity)) {
            return;
        }
        const auto index = sparse_[entityIndex(entity)];
        if (index != values_.size() - 1) {
            values_[index] = std::move(values_.back());
            entities_[index] = entities_.back();
            sparse_[entityIndex(entities_[index])] = index;
        }
        values_.pop_back();
        entities_.pop_back();
        sparse_[entityIndex(entity)] = absent;
    }

    template <class Compare> void sort(Compare compare) {
        if (std::is_sorted(values_.begin(), values_.end(), compare)) {
            return;
        }
        auto order = entities_;
        std::sort(order.begin(), order.end(), [&](Entity first, Entity second) {
            return compare(*get(first), *get(second));
        });
        for (std::size_t index = 0; index < order.size(); ++index) {
            const auto source = sparse_[entityIndex(order[index])];
            if (source == index) {
                continue;
            }
            std::swap(values_[index], values_[source]);
            std::swap(entities_[index], entities_[source]);
            sparse_[entityIndex(entities_[index])] = index;
            sparse_[entityIndex(entities_[source])] = source;
        }
    }

    std::vector<T> values_;
    std::vector<Entity> entities_;
    std::vector<std::size_t> sparse_;
};

class Registry final {
public:
    Registry() = default;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) = delete;
    Registry& operator=(Registry&&) = delete;

    void reserveEntities(std::size_t count) {
        requireStructuralAccess();
        slots_.reserve(count);
        commands_.reserve(count);
    }

    [[nodiscard]] Entity create() {
        std::uint32_t index = freeHead_;
        if (index == noSlot) {
            if (slots_.size() >= noSlot) {
                throw std::length_error("ECS entity index capacity exceeded.");
            }
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back({});
        } else {
            freeHead_ = slots_[index].nextFree;
        }
        auto& slot = slots_[index];
        slot.alive = true;
        slot.pending = false;
        ++living_;
        return (static_cast<Entity>(slot.generation) << 32) | (static_cast<Entity>(index) + 1);
    }

    [[nodiscard]] bool valid(Entity entity) const noexcept {
        const auto index = entityIndex(entity);
        return index < slots_.size() && slots_[index].alive && !slots_[index].pending &&
               slots_[index].generation == entityGeneration(entity);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return living_;
    }

    // Retained vector capacity only; excludes allocator/map overhead and allocations owned by individual components.
    [[nodiscard]] std::size_t capacityBytes() const noexcept {
        std::size_t bytes = slots_.capacity() * sizeof(Slot) + commands_.capacity() * sizeof(Command);
        for (const auto& [type, pool] : pools_) {
            bytes += pool->capacityBytes();
        }
        return bytes;
    }

    template <class T> [[nodiscard]] std::span<const Entity> entities() const noexcept {
        const auto* pool = const_cast<Registry*>(this)->findStorage<T>();
        return pool ? pool->entities() : std::span<const Entity>{};
    }

    bool destroy(Entity entity) {
        if (!valid(entity)) {
            return false;
        }
        if (iterationDepth_) {
            commands_.push_back({entity, nullptr});
            slots_[entityIndex(entity)].pending = true;
            --living_;
        } else {
            destroyNow(entity);
        }
        return true;
    }

    void clear() {
        requireStructuralAccess();
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            const auto& slot = slots_[index];
            if (slot.alive) {
                destroyNow((static_cast<Entity>(slot.generation) << 32) | (index + 1));
            }
        }
    }

    template <class T> Storage<T>& storage() {
        if (auto* pool = findStorage<T>()) {
            return *pool;
        }
        requireStructuralAccess();
        auto pool = std::make_unique<Storage<T>>();
        auto* result = pool.get();
        pools_.emplace(std::type_index{typeid(T)}, std::move(pool));
        return *result;
    }

    template <class T> void reserve(std::size_t count) {
        requireStructuralAccess();
        storage<T>().reserve(count);
    }

    template <class T, class Compare> void sort(Compare compare) {
        requireStructuralAccess();
        storage<T>().sort(compare);
    }

    template <class T, class... Args> T& emplace(Entity entity, Args&&... args) {
        requireStructuralAccess();
        if (!valid(entity)) {
            throw std::invalid_argument("Cannot add a component to an invalid entity.");
        }
        return storage<T>().emplace(entity, std::forward<Args>(args)...);
    }

    template <class T> [[nodiscard]] T* get(Entity entity) noexcept {
        auto* pool = findStorage<T>();
        return valid(entity) && pool ? pool->get(entity) : nullptr;
    }

    template <class T> [[nodiscard]] const T* get(Entity entity) const noexcept {
        return const_cast<Registry*>(this)->get<T>(entity);
    }

    template <class T> [[nodiscard]] bool has(Entity entity) const noexcept {
        return get<T>(entity) != nullptr;
    }

    template <class T> bool remove(Entity entity) {
        auto* pool = findStorage<T>();
        if (!valid(entity) || !pool || !pool->get(entity)) {
            return false;
        }
        if (iterationDepth_) {
            commands_.push_back({entity, pool});
        } else {
            pool->erase(entity);
        }
        return true;
    }

    // Walk the smallest pool. Removal/destruction commits after the outermost view, including on exception.
    // Destruction invalidates the handle immediately; references remain stable until the view exits.
    // Adding components/reserving while a view is active is rejected to prevent relocation of callback arguments.
    template <class First, class... Rest, class Visitor> void each(Visitor&& visitor) {
        const auto pools = std::tuple{findStorage<First>(), findStorage<Rest>()...};
        if (!std::apply(
                [](auto*... pool) {
                    return ((pool != nullptr) && ...);
                },
                pools)) {
            return;
        }
        detail::ComponentPool* smallest = std::get<0>(pools);
        std::apply(
            [&](auto*... pool) {
                ((smallest = pool->size() < smallest->entities().size() ? pool : smallest), ...);
            },
            pools);
        Iteration iteration{*this};
        for (const auto entity : smallest->entities()) {
            if (valid(entity) && std::apply(
                                     [&](auto*... pool) {
                                         return ((pool->get(entity) != nullptr) && ...);
                                     },
                                     pools)) {
                std::apply(
                    [&](auto*... pool) {
                        std::invoke(visitor, entity, *pool->get(entity)...);
                    },
                    pools);
            }
        }
    }

private:
    static constexpr std::uint32_t noSlot = std::numeric_limits<std::uint32_t>::max();

    struct Slot {
        std::uint32_t generation = 0, nextFree = noSlot;
        bool alive = false, pending = false;
    };

    struct Command {
        Entity entity;
        detail::ComponentPool* pool; // Null means destroy the entity and all its components.
    };

    struct Iteration {
        Registry& registry;

        explicit Iteration(Registry& value) : registry(value) {
            ++registry.iterationDepth_;
        }

        ~Iteration() {
            if (--registry.iterationDepth_ == 0) {
                registry.flush();
            }
        }
    };

    void requireStructuralAccess() const {
        if (iterationDepth_) {
            throw std::logic_error("ECS insertion, clear and reserve must happen outside active views.");
        }
    }

    template <class T> Storage<T>* findStorage() noexcept {
        const auto found = pools_.find(std::type_index{typeid(T)});
        return found == pools_.end() ? nullptr : static_cast<Storage<T>*>(found->second.get());
    }

    void destroyNow(Entity entity) noexcept {
        auto& slot = slots_[entityIndex(entity)];
        if (!slot.alive || slot.generation != entityGeneration(entity)) {
            return;
        }
        for (auto& [type, pool] : pools_) {
            pool->erase(entity);
        }
        living_ -= slot.pending ? 0 : 1;
        slot.alive = slot.pending = false;
        // Retire exhausted slots rather than letting a generation wrap and resurrect a stale handle.
        if (slot.generation != std::numeric_limits<std::uint32_t>::max()) {
            ++slot.generation;
            slot.nextFree = freeHead_;
            freeHead_ = entityIndex(entity);
        }
    }

    void flush() noexcept {
        for (const auto& command : commands_) {
            if (command.pool) {
                command.pool->erase(command.entity);
            } else {
                destroyNow(command.entity);
            }
        }
        commands_.clear();
    }

    std::vector<Slot> slots_;
    std::unordered_map<std::type_index, std::unique_ptr<detail::ComponentPool>> pools_;
    std::vector<Command> commands_;
    std::uint32_t freeHead_ = noSlot;
    std::size_t living_ = 0, iterationDepth_ = 0;
};

} // namespace grimlock::ecs
