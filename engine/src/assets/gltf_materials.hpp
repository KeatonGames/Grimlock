#pragma once

#include "grimlock/assets/model_data.hpp"
#include <filesystem>

namespace fastgltf { class Asset; }

namespace grimlock::assets {
// Importer-internal conversion; no fastgltf or Vulkan objects enter ModelData.
void loadGltfMaterials(const fastgltf::Asset& asset, const std::filesystem::path& directory, ModelData& model);
}
