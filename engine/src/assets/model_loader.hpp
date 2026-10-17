#pragma once

#include "grimlock/assets/model_data.hpp"

#include <filesystem>

namespace grimlock::assets {

// Returns engine-owned CPU data. No Vulkan state or importer types escape.
[[nodiscard]] ModelData loadObj(const std::filesystem::path& path);
[[nodiscard]] ModelData loadGltf(const std::filesystem::path& path);
[[nodiscard]] ModelData loadModel(const std::filesystem::path& path);

} // namespace grimlock::assets
