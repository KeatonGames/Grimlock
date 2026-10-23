#pragma once

#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace grimlock::game {

inline void validateDamage(float damage) {
    if (!std::isfinite(damage) || damage < 0) {
        throw std::invalid_argument("Damage must be finite and nonnegative.");
    }
}

// Saturating subtraction is shared by combat bodies and the integer-health objective.
template <typename T> bool receiveDamage(T& health, T damage) {
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(damage) || damage < 0) {
            throw std::invalid_argument("Damage must be finite and nonnegative.");
        }
    }
    const bool alive = health > 0;
    health = damage >= health ? T{0} : health - damage;
    return alive && health == 0;
}

} // namespace grimlock::game
