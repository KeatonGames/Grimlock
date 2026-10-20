#include "platform/window.hpp"

#include "core/startup_profile.hpp"

#include <stdexcept>
#include <string>

namespace grimlock::platform {

Window::Window(const char* title, const int width, const int height) {
    diagnostics::StartupSpan profile{"platform.SDL_window"};

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string{"SDL_Init failed: "} + SDL_GetError());
    }

    window_ = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    if (window_ == nullptr) {
        const std::string error = SDL_GetError();
        SDL_Quit();
        throw std::runtime_error("SDL_CreateWindow failed: " + error);
    }
}

Window::~Window() {
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
    }

    SDL_Quit();
}

SDL_Window* Window::native() const noexcept {
    return window_;
}

} // namespace grimlock::platform
