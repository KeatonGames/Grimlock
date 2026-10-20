#pragma once
#include "grimlock/input.hpp"

#include <SDL3/SDL.h>

namespace grimlock::platform {
// Owns only SDL mouse-mode state; Window must outlive it. Engine polls events.
class Input final {
public:
    explicit Input(SDL_Window* window);
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    void beginFrame();
    void handleEvent(const SDL_Event& event);
    void finishFrame(bool navigationEnabled);
    void cancelNavigation();
    void capture(bool mouse, bool keyboard);

    [[nodiscard]] const InputState& state() const noexcept { return state_; }

private:
    void clearButtons(bool keyboard);
    void relativeMode(bool enabled);
    SDL_Window* window_;
    SDL_WindowID id_;
    InputState state_;
    glm::vec2 dragPosition_{};
    bool relative_ = false;
};
} // namespace grimlock::platform
