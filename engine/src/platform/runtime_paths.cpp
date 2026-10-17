#include "platform/runtime_paths.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

namespace grimlock::platform {

std::filesystem::path utf8Path(const std::string_view path) {
    return std::filesystem::path{std::u8string{path.begin(), path.end()}};
}

std::filesystem::path runtimePath(const std::filesystem::path& relative) {
    if (relative.is_absolute()) { return relative; }
    const char* base = SDL_GetBasePath();
    if (base == nullptr) { throw std::runtime_error(std::string{"SDL_GetBasePath failed: "} + SDL_GetError()); }
    return utf8Path(base) / relative;
}

} // namespace grimlock::platform
