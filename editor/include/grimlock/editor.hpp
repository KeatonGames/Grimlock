#pragma once
#include <filesystem>
namespace grimlock::editor {
// Optional launcher; Game code never needs editor implementation types.
void run(const std::filesystem::path& scene = {}, const std::filesystem::path& environment = {});
}
