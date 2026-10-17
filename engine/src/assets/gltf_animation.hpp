#pragma once
#include "grimlock/assets/model_data.hpp"
namespace fastgltf { class Asset; }
namespace grimlock::assets {
void loadGltfAnimation(const fastgltf::Asset& asset, ModelData& model);
}
