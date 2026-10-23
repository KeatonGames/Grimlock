#pragma once

#include <grimlock/input.hpp>

namespace grimlock::game {

// A release gesture, independent of rendering/SDL. Camera uses the same threshold.
class GroundCommandGesture final {
public:
    static constexpr float dragThreshold = 5;

    [[nodiscard]] std::optional<glm::vec2> update(const InputState& input, bool viewport) {
        const auto& right = input.button(MouseButton::right);
        if (right.pressed) {
            armed_ = viewport && input.focused && input.rightPressPosition.has_value();
        }
        if (!viewport || !input.focused || input.button(MouseButton::middle).held ||
            input.button(MouseButton::middle).pressed || input.mouseWheel != glm::vec2{0} ||
            input.rightDragDistance > dragThreshold) {
            armed_ = false;
        }
        if (right.released) {
            const bool command = armed_;
            armed_ = false;
            return command ? input.rightPressPosition : std::nullopt;
        }
        return {};
    }

private:
    bool armed_ = false;
};

} // namespace grimlock::game
