#pragma once
#include <glm/vec2.hpp>
#include <array>
#include <cstddef>

namespace grimlock {
enum class MouseButton : std::size_t { left, middle, right, x1, x2 };
struct ButtonState {
    bool pressed = false, held = false, released = false;
};
// One immutable snapshot per Engine::update(). Keyboard indices are physical SDL
// scancodes, not text/keycodes; no keyboard or left-button actions are mapped.
struct InputState {
    static constexpr std::size_t keyCount = 512;
    std::array<ButtonState,5> mouseButtons{};
    std::array<ButtonState,keyCount> keys{};
    glm::vec2 mousePosition{}, mouseMotion{}, mouseWheel{};
    bool focused = false;
    [[nodiscard]] const ButtonState& button(MouseButton button) const { return mouseButtons.at(static_cast<std::size_t>(button)); }
    [[nodiscard]] const ButtonState& key(std::size_t scancode) const { return keys.at(scancode); }
};
}
