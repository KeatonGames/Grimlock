#include "systems/economy_system.hpp"

#include <limits>
#include <stdexcept>

namespace grimlock::game {

bool EconomySystem::trySpend(std::uint64_t cost) noexcept {
    if (!canAfford(cost)) {
        return false;
    }
    gold_ -= cost;
    return true;
}

void EconomySystem::earn(std::uint64_t amount) {
    if (amount > std::numeric_limits<std::uint64_t>::max() - gold_) {
        throw std::overflow_error("Gold balance would overflow.");
    }
    gold_ += amount;
}

} // namespace grimlock::game
