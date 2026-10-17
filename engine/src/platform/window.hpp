#pragma once

#include <SDL3/SDL.h>

namespace grimlock::platform {

class Window final {
public:
    Window(const char* title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    [[nodiscard]] SDL_Window* native() const noexcept;

private:
    SDL_Window* window_ = nullptr;
};

} // namespace grimlock::platform
