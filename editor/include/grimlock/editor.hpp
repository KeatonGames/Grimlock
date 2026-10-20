#pragma once

#include <filesystem>

namespace grimlock::editor {

class GridDebug;

// Optional launcher; Game code never needs editor implementation types.
void run(const std::filesystem::path& scene = {},
         const std::filesystem::path& environment = {},
         GridDebug* gridDebug = nullptr);

} // namespace grimlock::editor
