#include "systems/crowd_movement.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace grimlock::game {

namespace {

glm::dvec2 horizontal(glm::dvec3 value) {
    return {value.x, value.z};
}

double contactFraction(glm::dvec2 offset, glm::dvec2 displacement, double distance) {
    const double a = glm::dot(displacement, displacement);
    const double b = glm::dot(offset, displacement);
    const double c = glm::dot(offset, offset) - distance * distance;
    if (a < 1e-20 || b >= 0) {
        return 1;
    }
    if (c <= 0) {
        return 0;
    }
    const double discriminant = b * b - a * c;
    return discriminant <= 0 ? 1 : std::clamp((-b - std::sqrt(discriminant)) / a - 1e-6, 0.0, 1.0);
}

} // namespace

glm::i64vec2 EnemyNeighbors::key(glm::dvec3 position) const {
    return {static_cast<std::int64_t>(std::floor((position.x - origin_.x) / binSize_)),
            static_cast<std::int64_t>(std::floor((position.z - origin_.z) / binSize_))};
}

std::size_t EnemyNeighbors::find(glm::i64vec2 value) const {
    const auto x = static_cast<std::uint64_t>(value.x), z = static_cast<std::uint64_t>(value.y);
    auto slot = static_cast<std::size_t>((x * 0x9e3779b97f4a7c15ULL) ^ (z * 0xc2b2ae3d27d4eb4fULL));
    slot &= buckets_.size() - 1;
    while (buckets_[slot].used && buckets_[slot].key != value) {
        slot = (slot + 1) & (buckets_.size() - 1);
    }
    return slot;
}

void EnemyNeighbors::insert(std::size_t index, glm::dvec3 position) {
    const auto value = key(position);
    auto& bucket = buckets_[find(value)];
    bucket.used = true;
    bucket.key = value;
    next_[index] = bucket.first;
    bucket.first = index;
}

void EnemyNeighbors::build(std::span<const Enemy> enemies, double binSize, glm::dvec3 origin, std::size_t capacity) {
    binSize_ = binSize;
    origin_ = origin;
    capacity = std::max(capacity, enemies.size());
    buckets_.assign(std::bit_ceil(std::max(std::size_t{8}, capacity * 4)), Bucket{});
    next_.assign(capacity, none);
    for (std::size_t index = 0; index < enemies.size(); ++index) {
        if (enemies[index].active && enemies[index].alive) {
            insert(index, enemies[index].position);
        }
    }
}

void EnemyNeighbors::relocate(std::size_t index, glm::dvec3 before, glm::dvec3 after) {
    if (key(before) == key(after)) {
        return;
    }
    auto* link = &buckets_[find(key(before))].first;
    while (*link != index) {
        link = &next_[*link];
    }
    *link = next_[index];
    insert(index, after);
}

void CrowdMovement::update(const NavMesh& navigation,
                           std::optional<glm::dvec2> destination,
                           std::span<Enemy> enemies,
                           double delta,
                           bool despawnOnArrival) {
    if (!std::isfinite(delta) || delta < 0) {
        throw std::invalid_argument("Crowd delta must be finite and nonnegative.");
    }
    if (destination && (!std::isfinite(destination->x) || !std::isfinite(destination->y))) {
        throw std::invalid_argument("Enemy destination must contain finite world X/Z coordinates.");
    }
    metrics_ = {};
    if (enemies.empty()) {
        return;
    }
    double minimumRadius = std::numeric_limits<double>::max(), maximumRadius = 0, maximumSpeed = 0;
    for (const auto& enemy : enemies) {
        if (!std::isfinite(enemy.radius) || enemy.radius <= 0 || !std::isfinite(enemy.speed) || enemy.speed < 0 ||
            !std::isfinite(enemy.acceleration) || enemy.acceleration <= 0 || !std::isfinite(enemy.deceleration) ||
            enemy.deceleration <= 0 || !std::isfinite(enemy.position.x) || !std::isfinite(enemy.position.y) ||
            !std::isfinite(enemy.position.z) || !std::isfinite(enemy.velocity.x) || !std::isfinite(enemy.velocity.y) ||
            !std::isfinite(enemy.yaw)) {
            throw std::invalid_argument("Enemy movement parameters and state must be finite and valid.");
        }
        minimumRadius = std::min(minimumRadius, enemy.radius);
        maximumRadius = std::max(maximumRadius, enemy.radius);
        maximumSpeed = std::max({maximumSpeed, enemy.speed, glm::length(enemy.velocity)});
    }
    const double limit = maximumSpeed > 0 ? std::min(1.0 / 60, minimumRadius * 0.4 / maximumSpeed) : 1.0 / 60;
    // Bound recovery work after a stalled frame; excess elapsed time is intentionally discarded.
    delta = std::min({delta, 0.25, limit * 64});
    do {
        const double dt = std::min(delta, limit);
        step(navigation, destination, enemies, dt, maximumRadius, despawnOnArrival);
        delta -= dt;
        ++metrics_.substeps;
    } while (delta > 1e-10);
}

void CrowdMovement::step(const NavMesh& navigation,
                         std::optional<glm::dvec2> destination,
                         std::span<Enemy> enemies,
                         double delta,
                         double maximumRadius,
                         bool despawnOnArrival) {
    neighbors_.build(enemies, maximumRadius * 2, glm::dvec3{0});
    resolveOverlaps(navigation, enemies, maximumRadius);
    remaining_.assign(enemies.size(), std::numeric_limits<double>::max());
    for (std::size_t index = 0; index < enemies.size(); ++index) {
        auto& enemy = enemies[index];
        enemy.desiredVelocity = {0, 0};
        if (!enemy.active || !enemy.alive) {
            continue;
        }
        enemy.repathDelay = std::max(0.0, enemy.repathDelay - delta);
        if (!navigation.contains(enemy.position) || enemy.radius > navigation.configuration().agentRadius + 1e-6) {
            enemy.movement = MovementStatus::blocked;
            enemy.velocity = {0, 0};
            continue;
        }
        if (!destination) {
            enemy.movement = MovementStatus::idle;
            enemy.velocity = {0, 0};
            enemy.requestedDestination.reset();
            enemy.path.valid = false;
            continue;
        }
        const bool changed = enemy.requestedDestination != destination || enemy.path.revision != navigation.revision();
        if (changed || (enemy.retryPath && enemy.repathDelay <= 0)) {
            const glm::dvec3 goal{destination->x, enemy.position.y, destination->y};
            const bool found = navigation.findPath(enemy.position, goal, enemy.path);
            enemy.requestedDestination = destination;
            enemy.retryPath = false;
            enemy.repathDelay = 1;
            enemy.stuckTime = 0;
            if (!found) {
                enemy.velocity = {0, 0};
            }
        }
        double remaining = 0;
        const auto target = navigation.steering(enemy.position, enemy.path, &remaining);
        if (!target) {
            // A failed search waits for a destination/mesh change. A displaced
            // corridor requests a bounded retry, rather than searching every frame.
            enemy.retryPath = enemy.retryPath || !enemy.path.polygons.empty();
            enemy.movement = MovementStatus::blocked;
            enemy.velocity = {0, 0};
            continue;
        }
        remaining_[index] = remaining;
        if (remaining <= std::max(1e-4, enemy.radius)) {
            enemy.movement = MovementStatus::arrived;
            enemy.velocity = {0, 0};
            enemy.active = !despawnOnArrival;
            continue;
        }
        const auto offset = horizontal(*target - enemy.position);
        const double distance = glm::length(offset);
        if (distance < 1e-6) {
            enemy.retryPath = true;
            enemy.path.valid = false;
            enemy.movement = MovementStatus::blocked;
            enemy.velocity = {0, 0};
            continue;
        }
        enemy.movement = MovementStatus::idle;
        const double speed = std::min(enemy.speed, std::sqrt(2 * enemy.deceleration * remaining));
        enemy.desiredVelocity = offset * (speed / distance);
    }

    for (std::size_t order = 0; order < enemies.size(); ++order) {
        const auto index = static_cast<std::size_t>((order + order_) % enemies.size());
        auto& enemy = enemies[index];
        if (!destination || !enemy.active || !enemy.alive || enemy.movement == MovementStatus::blocked ||
            enemy.movement == MovementStatus::arrived) {
            continue;
        }
        auto desired = enemy.desiredVelocity;
        if (enemy.speed > 0) {
            desired += separation(index, enemies, maximumRadius, enemy.radius * 0.025) * enemy.speed * 1.4;
        }
        const double speed = glm::length(desired);
        if (speed > enemy.speed && speed > 0) {
            desired *= enemy.speed / speed;
        }
        // Preview wall sliding to start turning or braking before physical contact.
        desired = horizontal(navigation.move(enemy.position, desired * 0.2) - enemy.position) / 0.2;
        enemy.desiredVelocity = desired;
        const auto change = desired - enemy.velocity;
        const double difference = glm::length(change);
        const bool braking = glm::dot(change, enemy.velocity) < 0;
        const double acceleration = braking ? enemy.deceleration : enemy.acceleration;
        enemy.velocity += difference > 0 ? change * std::min(1.0, acceleration * delta / difference) : glm::dvec2{0};
        const auto before = enemy.position;
        enemy.position = move(navigation, index, enemies, enemy.velocity * delta, maximumRadius);
        const auto displacement = horizontal(enemy.position - before);
        if (delta > 0) {
            enemy.velocity = displacement / delta;
        }
        const double traveled = glm::length(displacement);
        enemy.stuckTime = enemy.speed > 0 && traveled < enemy.speed * delta * 0.02 ? enemy.stuckTime + delta : 0;
        if (enemy.stuckTime > 2 && enemy.repathDelay <= 0) {
            enemy.retryPath = true;
            enemy.stuckTime = 0;
        }
        enemy.movement = glm::length(enemy.velocity) > 1e-4 ? MovementStatus::moving : MovementStatus::idle;
        if (enemy.movement == MovementStatus::moving) {
            const double facing = std::atan2(enemy.velocity.x, enemy.velocity.y);
            enemy.yaw += std::remainder(facing - enemy.yaw, 2 * std::numbers::pi) * -std::expm1(-10 * delta);
        }
        neighbors_.relocate(index, before, enemy.position);
    }
    ++order_;
}

glm::dvec2 CrowdMovement::separation(std::size_t index,
                                     std::span<const Enemy> enemies,
                                     double maximumRadius,
                                     double padding) {
    const auto& enemy = enemies[index];
    glm::dvec2 force{0};
    const double range = (enemy.radius + maximumRadius + padding) * 1.5;
    neighbors_.visit(enemy.position, range, [&](std::size_t otherIndex) {
        if (index == otherIndex || !enemies[otherIndex].active || !enemies[otherIndex].alive) {
            return;
        }
        ++metrics_.neighborChecks;
        const auto away = horizontal(enemy.position - enemies[otherIndex].position);
        const double distance = glm::length(away);
        const double comfort = (enemy.radius + enemies[otherIndex].radius + padding) * 1.5;
        if (distance > 1e-10 && distance < comfort) {
            const double weight = (comfort - distance) / comfort;
            const bool yield =
                remaining_[otherIndex] < remaining_[index] - 1e-6 ||
                (std::abs(remaining_[otherIndex] - remaining_[index]) <= 1e-6 && enemies[otherIndex].id < enemy.id);
            force += away / distance * weight * (yield ? 3.0 : 0.2);
        }
    });
    return force;
}

glm::dvec3 CrowdMovement::move(const NavMesh& navigation,
                               std::size_t index,
                               std::span<const Enemy> enemies,
                               glm::dvec2 displacement,
                               double maximumRadius) {
    const auto& enemy = enemies[index];
    auto position = enemy.position;
    const double skin = std::max(1e-8, enemy.radius * 1e-5);
    for (int contact = 0; contact < 4 && glm::dot(displacement, displacement) > 1e-22; ++contact) {
        displacement = horizontal(navigation.move(position, displacement) - position);
        double fraction = 1;
        std::optional<std::size_t> neighbor;
        neighbors_.visit(position,
                         enemy.radius + maximumRadius + glm::length(displacement) + skin,
                         [&](std::size_t other) {
                             if (index == other || !enemies[other].active || !enemies[other].alive) {
                                 return;
                             }
                             ++metrics_.neighborChecks;
                             const double t = contactFraction(horizontal(position - enemies[other].position),
                                                              displacement,
                                                              enemy.radius + enemies[other].radius + skin);
                             if (t < fraction) {
                                 fraction = t;
                                 neighbor = other;
                             }
                         });
        position = navigation.move(position, displacement * fraction);
        if (!neighbor) {
            break;
        }
        const auto away = horizontal(position - enemies[*neighbor].position);
        const double distance = glm::length(away);
        const auto normal = distance > 1e-10 ? away / distance : glm::dvec2{1, 0};
        displacement *= 1 - fraction;
        displacement -= normal * std::min(0.0, glm::dot(displacement, normal));
    }
    return position;
}

void CrowdMovement::resolveOverlaps(const NavMesh& navigation, std::span<Enemy> enemies, double maximumRadius) {
    for (int iteration = 0; iteration < 3; ++iteration) {
        bool corrected = false;
        for (std::size_t index = 0; index < enemies.size(); ++index) {
            auto& enemy = enemies[index];
            if (!enemy.active || !enemy.alive || !navigation.contains(enemy.position) ||
                enemy.radius > navigation.configuration().agentRadius + 1e-6) {
                continue;
            }
            glm::dvec2 correction{0};
            neighbors_.visit(enemy.position, enemy.radius + maximumRadius, [&](std::size_t other) {
                if (index == other || !enemies[other].active || !enemies[other].alive) {
                    return;
                }
                ++metrics_.neighborChecks;
                const auto offset = horizontal(enemy.position - enemies[other].position);
                const double distance = glm::length(offset);
                const double overlap = enemy.radius + enemies[other].radius - distance;
                if (overlap > 1e-8) {
                    const auto normal =
                        distance > 1e-10 ? offset / distance : glm::dvec2{enemy.id < enemies[other].id ? 1 : -1, 0};
                    correction += normal * (overlap + 1e-6);
                }
            });
            const double distance = glm::length(correction);
            if (distance < 1e-10) {
                continue;
            }
            correction *= std::min(1.0, enemy.radius * 0.5 / distance);
            const auto before = enemy.position;
            enemy.position = navigation.move(enemy.position, correction);
            neighbors_.relocate(index, before, enemy.position);
            corrected = corrected || enemy.position != before;
        }
        if (!corrected) {
            break;
        }
    }
}

} // namespace grimlock::game
