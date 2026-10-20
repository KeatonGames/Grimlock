#pragma once

namespace fastgltf {

class Asset;
}

namespace grimlock::assets {

struct ModelData;

void loadGltfAnimation(const fastgltf::Asset& asset, ModelData& model);

} // namespace grimlock::assets
