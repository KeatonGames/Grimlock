#pragma once

#include <filesystem>
#include <string_view>

namespace grimlock::platform {

[[nodiscard]] std::filesystem::path utf8Path(std::string_view path);
// Absolute paths pass through; relative paths are resolved beside the executable.
[[nodiscard]] std::filesystem::path runtimePath(const std::filesystem::path& relative);

} // namespace grimlock::platform
