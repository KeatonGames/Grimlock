#include "input/input.hpp"

#include <cmath>
#include <iostream>

namespace grimlock::platform {
namespace {
void release(ButtonState& b) {
    b.released = b.released || b.held;
    b.held = b.pressed = false;
}

void transition(ButtonState& b, bool down) {
    if (down && !b.held) {
        b.pressed = true;
    } else if (!down && b.held) {
        b.released = true;
    }
    b.held = down;
}

bool finite(float x, float y) {
    return std::isfinite(x) && std::isfinite(y);
}
} // namespace

Input::Input(SDL_Window* window) : window_(window), id_(SDL_GetWindowID(window)) {
    static_assert(SDL_SCANCODE_COUNT == InputState::keyCount);
    state_.focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    SDL_GetMouseState(&state_.mousePosition.x, &state_.mousePosition.y);
}

Input::~Input() {
    relativeMode(false);
}

void Input::beginFrame() {
    state_.leftPressPosition.reset();
    for (auto& b : state_.mouseButtons) {
        b.pressed = b.released = false;
    }
    for (auto& b : state_.keys) {
        b.pressed = b.released = false;
    }
    state_.mouseMotion = state_.mouseWheel = {0, 0};
}

void Input::clearButtons(bool keyboard) {
    state_.leftPressPosition.reset();
    for (auto& b : state_.mouseButtons) {
        release(b);
    }
    if (keyboard) {
        for (auto& b : state_.keys) {
            release(b);
        }
    }
    state_.mouseMotion = state_.mouseWheel = {0, 0};
    relativeMode(false);
}

void Input::handleEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (event.window.windowID == id_) {
            state_.focused = true;
            clearButtons(true);
            SDL_GetMouseState(&state_.mousePosition.x, &state_.mousePosition.y);
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MINIMIZED:
        if (event.window.windowID == id_) {
            state_.focused = false;
            clearButtons(true);
        }
        break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (event.window.windowID == id_) {
            clearButtons(false);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.windowID == id_ && state_.focused && event.button.button >= 1 &&
            event.button.button <= state_.mouseButtons.size()) {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT && !state_.button(MouseButton::left).held &&
                !state_.leftPressPosition && finite(event.button.x, event.button.y)) {
                state_.leftPressPosition = glm::vec2{event.button.x, event.button.y};
            }
            transition(state_.mouseButtons[event.button.button - 1],
                       event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            if (finite(event.button.x, event.button.y)) {
                state_.mousePosition = {event.button.x, event.button.y};
            }
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (event.motion.windowID == id_ && state_.focused &&
            finite(event.motion.x, event.motion.y) &&
            finite(event.motion.xrel, event.motion.yrel)) {
            state_.mousePosition = {event.motion.x, event.motion.y};
            state_.mouseMotion += glm::vec2{event.motion.xrel, event.motion.yrel};
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (event.wheel.windowID == id_ && state_.focused && finite(event.wheel.x, event.wheel.y)) {
            const float sign = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
            state_.mouseWheel += sign * glm::vec2{event.wheel.x, event.wheel.y};
            if (finite(event.wheel.mouse_x, event.wheel.mouse_y)) {
                state_.mousePosition = {event.wheel.mouse_x, event.wheel.mouse_y};
            }
        }
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (event.key.windowID == id_ && state_.focused && event.key.scancode >= 0 &&
            static_cast<std::size_t>(event.key.scancode) < state_.keys.size()) {
            transition(state_.keys[event.key.scancode], event.type == SDL_EVENT_KEY_DOWN);
        }
        break;
    default:
        break;
    }
}

void Input::relativeMode(bool enabled) {
    if (relative_ == enabled) {
        return;
    }
    if (enabled) {
        dragPosition_ = state_.mousePosition;
    } else if (state_.focused && SDL_GetKeyboardFocus() == window_) {
        SDL_WarpMouseInWindow(window_, dragPosition_.x, dragPosition_.y);
    }
    if (!SDL_SetWindowRelativeMouseMode(window_, enabled)) {
        std::clog << "Input warning: relative mouse mode failed: " << SDL_GetError() << '\n';
        if (enabled) {
            (void)SDL_SetWindowRelativeMouseMode(window_, false);
            relative_ = SDL_GetWindowRelativeMouseMode(window_);
            release(state_.mouseButtons[1]);
            release(state_.mouseButtons[2]);
            state_.mouseMotion = {0, 0};
        }
        return;
    }
    relative_ = enabled;
    state_.mouseMotion = {0, 0};
    if (!enabled) {
        SDL_GetMouseState(&state_.mousePosition.x, &state_.mousePosition.y);
    }
}

void Input::finishFrame(bool navigationEnabled) {
    relativeMode(
        navigationEnabled && state_.focused &&
        (state_.button(MouseButton::right).held || state_.button(MouseButton::middle).held));
}

void Input::cancelNavigation() {
    release(state_.mouseButtons[1]);
    release(state_.mouseButtons[2]);
    state_.mouseMotion = {0, 0};
    relativeMode(false);
}

void Input::capture(bool mouse, bool keyboard) {
    if (mouse) {
        clearButtons(false);
    }
    if (keyboard) {
        for (auto& button : state_.keys) {
            button = {};
        }
    }
}
} // namespace grimlock::platform
