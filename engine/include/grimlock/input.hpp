#pragma once

#include <array>
#include <cstddef>
#include <glm/vec2.hpp>
#include <optional>

namespace grimlock {

enum class MouseButton : std::size_t {
    left,
    middle,
    right,
    x1,
    x2
};

struct ButtonState {
    bool pressed = false, held = false, released = false;
};

// One immutable snapshot per Engine::update(). Keyboard indices are physical SDL
// scancodes, not text/keycodes. Action policy stays in Engine/Game.
struct InputState {
    static constexpr std::size_t keyCount = 512;
    std::array<ButtonState, 5> mouseButtons{};
    std::array<ButtonState, keyCount> keys{};
    glm::vec2 mousePosition{}, mouseMotion{}, mouseWheel{};
    std::optional<glm::vec2> leftPressPosition;  // First press coordinates, not later drag motion.
    std::optional<glm::vec2> rightPressPosition; // Retained through release, including relative mouse mode.
    float rightDragDistance = 0; // Total motion since right press; returning to the start still counts as a drag.
    bool focused = false;

    [[nodiscard]] const ButtonState& button(MouseButton button) const {
        return mouseButtons.at(static_cast<std::size_t>(button));
    }

    [[nodiscard]] const ButtonState& key(std::size_t scancode) const {
        return keys.at(scancode);
    }
};

} // namespace grimlock
