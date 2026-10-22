#pragma once

#include <cstdint>

namespace grimlock::game {

class EconomySystem final {
public:
    void reset(std::uint64_t gold = 0) noexcept {
        gold_ = gold;
    }

    [[nodiscard]] std::uint64_t gold() const noexcept {
        return gold_;
    }

    [[nodiscard]] bool canAfford(std::uint64_t cost) const noexcept {
        return cost <= gold_;
    }

    [[nodiscard]] bool trySpend(std::uint64_t cost) noexcept;
    void earn(std::uint64_t amount);

private:
    std::uint64_t gold_ = 0;
};

} // namespace grimlock::game
