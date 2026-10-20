#pragma once

#include <filesystem>

namespace fastgltf {
class Asset;
}

namespace grimlock::assets {
struct ModelData;

// Importer-internal conversion; no fastgltf or Vulkan objects enter ModelData.
void loadGltfMaterials(const fastgltf::Asset& asset, const std::filesystem::path& directory,
                       ModelData& model);
} // namespace grimlock::assets
