#include "assets/gltf_materials.hpp"

#include "grimlock/assets/model_data.hpp"

#include <array>
#include <cstring>
#include <fastgltf/types.hpp>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace grimlock::assets;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

fastgltf::TextureInfo textureInfo(std::size_t texture = 0) {
    fastgltf::TextureInfo info;
    info.textureIndex = texture;
    return info;
}

fastgltf::Asset makeAsset() {
    fastgltf::Asset asset;
    // A one-pixel red RGBA PNG; the second image is invalid and intentionally unused.
    constexpr std::array<unsigned char, 70> png{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0d, 0x49, 0x44, 0x41, 0x54, 0x18, 0x57, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff,
        0xa6, 0x5c, 0x9b, 0x5d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    fastgltf::sources::Array data{fastgltf::StaticVector<std::byte>(png.size())};
    std::memcpy(data.bytes.data(), png.data(), png.size());
    asset.images.resize(2);
    asset.images[0].name = "red";
    asset.images[0].data = std::move(data);
    asset.textures.resize(2);
    for (auto& texture : asset.textures) {
        texture.imageIndex = std::size_t{0};
    }

    fastgltf::Material material;
    material.name = "layered";
    material.unlit = true;
    material.emissiveStrength = 2;
    material.ior = 1.25F;
    material.doubleSided = true;
    material.alphaMode = fastgltf::AlphaMode::Mask;
    material.alphaCutoff = 0.25F;
    material.pbrData.baseColorFactor = {0.1F, 0.2F, 0.3F, 0.4F};
    material.pbrData.metallicFactor = 0.5F;
    material.pbrData.roughnessFactor = 0.6F;
    material.emissiveFactor = {0.2F, 0.3F, 0.4F};
    auto base = textureInfo();
    base.transform = std::make_unique<fastgltf::TextureTransform>();
    base.transform->rotation = 0.25F;
    base.transform->uvOffset = {0.1F, 0.2F};
    base.transform->uvScale = {2, 3};
    base.transform->texCoordIndex = std::size_t{1};
    material.pbrData.baseColorTexture = std::move(base);
    material.pbrData.metallicRoughnessTexture = textureInfo(1);
    fastgltf::NormalTextureInfo normal;
    normal.textureIndex = 0;
    normal.scale = 0.7F;
    material.normalTexture = std::move(normal);
    fastgltf::OcclusionTextureInfo occlusion;
    occlusion.textureIndex = 1;
    occlusion.strength = 0.8F;
    material.occlusionTexture = std::move(occlusion);
    material.emissiveTexture = textureInfo();

    material.specular = std::make_unique<fastgltf::MaterialSpecular>();
    material.specular->specularFactor = 0.9F;
    material.specular->specularColorFactor = {1.1F, 1.2F, 1.3F};
    material.specular->specularTexture = textureInfo();
    material.specular->specularColorTexture = textureInfo(1);
    material.clearcoat = std::make_unique<fastgltf::MaterialClearcoat>();
    material.clearcoat->clearcoatFactor = 0.2F;
    material.clearcoat->clearcoatRoughnessFactor = 0.3F;
    material.clearcoat->clearcoatTexture = textureInfo();
    material.clearcoat->clearcoatRoughnessTexture = textureInfo(1);
    fastgltf::NormalTextureInfo coatNormal;
    coatNormal.textureIndex = 1;
    coatNormal.scale = -0.5F;
    material.clearcoat->clearcoatNormalTexture = std::move(coatNormal);
    material.transmission = std::make_unique<fastgltf::MaterialTransmission>();
    material.transmission->transmissionFactor = 0.4F;
    material.transmission->transmissionTexture = textureInfo();
    material.volume = std::make_unique<fastgltf::MaterialVolume>();
    material.volume->thicknessFactor = 0.5F;
    material.volume->attenuationDistance = 3;
    material.volume->attenuationColor = {0.6F, 0.7F, 0.8F};
    material.volume->thicknessTexture = textureInfo(1);
    material.sheen = std::make_unique<fastgltf::MaterialSheen>();
    material.sheen->sheenColorFactor = {0.3F, 0.4F, 0.5F};
    material.sheen->sheenRoughnessFactor = 0.6F;
    material.sheen->sheenColorTexture = textureInfo();
    material.sheen->sheenRoughnessTexture = textureInfo(1);
    asset.materials.push_back(std::move(material));

    fastgltf::Material opaque;
    opaque.name = "opaque";
    asset.materials.push_back(std::move(opaque));
    fastgltf::Material blend;
    blend.name = "blend";
    blend.alphaMode = fastgltf::AlphaMode::Blend;
    asset.materials.push_back(std::move(blend));
    return asset;
}

void checkMaterials() {
    const auto asset = makeAsset();
    ModelData model;
    model.materials.front().name = "replace";
    model.warnings = "existing\n";
    loadGltfMaterials(asset, {}, model);
    require(model.materials.size() == 4 && model.materials[0].name == "layered" &&
                model.materials[1].name == "opaque" && model.materials[2].name == "blend" &&
                model.materials.back().name.empty(),
            "material replacement, ordering, or fallback changed");
    const auto& material = model.materials[0];
    require(material.baseColor == glm::vec4{0.1F, 0.2F, 0.3F, 0.4F} && material.metallic == 0.5F &&
                material.roughness == 0.6F && material.emissive == glm::vec3{0.2F, 0.3F, 0.4F} &&
                material.normalScale == 0.7F && material.occlusionStrength == 0.8F && material.doubleSided &&
                material.alphaCutoff == 0.25F && material.alphaMode == AlphaMode::mask &&
                model.materials[1].alphaMode == AlphaMode::opaque && model.materials[2].alphaMode == AlphaMode::blend,
            "core surface factors or alpha modes changed");
    require(material.unlit && material.emissiveStrength == 2 && material.ior == 1.25F && material.specular == 0.9F &&
                material.specularColor == glm::vec3{1.1F, 1.2F, 1.3F} && material.clearcoat == 0.2F &&
                material.clearcoatRoughness == 0.3F && material.clearcoatNormalScale == -0.5F &&
                material.transmission == 0.4F && material.thickness == 0.5F && material.attenuationDistance == 3 &&
                material.attenuationColor == glm::vec3{0.6F, 0.7F, 0.8F} &&
                material.sheenColor == glm::vec3{0.3F, 0.4F, 0.5F} && material.sheenRoughness == 0.6F,
            "material extension factors changed");
    const auto& base = *material.baseColorTexture;
    require(base.texture == 0 && base.uvSet == 1 && base.offset == glm::vec2{0.1F, 0.2F} &&
                base.scale == glm::vec2{2, 3} && base.rotation == 0.25F,
            "texture transform or UV override changed");
    for (const auto* slot : {&material.metallicRoughnessTexture,
                             &material.normalTexture,
                             &material.occlusionTexture,
                             &material.emissiveTexture,
                             &material.specularTexture,
                             &material.specularColorTexture,
                             &material.clearcoatTexture,
                             &material.clearcoatRoughnessTexture,
                             &material.clearcoatNormalTexture,
                             &material.transmissionTexture,
                             &material.thicknessTexture,
                             &material.sheenColorTexture,
                             &material.sheenRoughnessTexture}) {
        require(slot->has_value() && (*slot)->texture < 2 && (*slot)->uvSet == 0, "texture slot was lost");
    }
    require(model.textures.size() == 2 && model.textures[0].image == 0 && model.textures[1].image == 0 &&
                model.textures[0].sampler == SamplerData{} && model.images.size() == 2 && model.images[0] &&
                !model.images[1] && model.images[0]->width == 1 && model.images[0]->height == 1 &&
                model.images[0]->pixels == std::vector<std::uint8_t>{255, 0, 0, 255},
            "shared image mapping, lazy decoding, or sampler defaults changed");
    require(model.warnings ==
                "existing\nMaterial 'layered': transmission/volume/sheen data preserved but not rendered; "
                "core surface preview only.\n",
            "advanced-material warning changed");
    require(model.materials.back().baseColor == glm::vec4{1} && model.materials.back().metallic == 1 &&
                model.materials.back().roughness == 1 && !model.materials.back().baseColorTexture,
            "fallback material defaults changed");

    auto cachedAsset = makeAsset();
    cachedAsset.images[0].data = std::monostate{};
    ModelData cached;
    cached.images.resize(2);
    cached.images[0] = ImageData{"cached", 1, 1, {0, 0, 255, 255}};
    loadGltfMaterials(cachedAsset, {}, cached);
    require(cached.images[0]->name == "cached" && cached.images[0]->pixels[2] == 255,
            "predecoded shared image was replaced");
}

void checkSamplers() {
    fastgltf::Asset asset;
    asset.images.resize(1);
    const std::array filters{fastgltf::Filter::Nearest,
                             fastgltf::Filter::Linear,
                             fastgltf::Filter::NearestMipMapNearest,
                             fastgltf::Filter::LinearMipMapNearest,
                             fastgltf::Filter::NearestMipMapLinear,
                             fastgltf::Filter::LinearMipMapLinear};
    const std::array wraps{fastgltf::Wrap::Repeat, fastgltf::Wrap::MirroredRepeat, fastgltf::Wrap::ClampToEdge};
    for (std::size_t index = 0; index < filters.size(); ++index) {
        fastgltf::Sampler sampler;
        sampler.minFilter = filters[index];
        sampler.magFilter = index % 2 == 0 ? fastgltf::Filter::Nearest : fastgltf::Filter::Linear;
        sampler.wrapS = wraps[index % 3];
        sampler.wrapT = wraps[(index + 1) % 3];
        asset.samplers.push_back(std::move(sampler));
        fastgltf::Texture texture;
        texture.imageIndex = std::size_t{0};
        texture.samplerIndex = index;
        asset.textures.push_back(std::move(texture));
    }
    ModelData model;
    loadGltfMaterials(asset, {}, model);
    const std::array expectedWraps{TextureWrap::repeat, TextureWrap::mirroredRepeat, TextureWrap::clampToEdge};
    for (std::size_t index = 0; index < filters.size(); ++index) {
        const auto filter = index % 2 == 0 ? TextureFilter::nearest : TextureFilter::linear;
        const auto mip = index < 2 ? MipmapFilter::none : index < 4 ? MipmapFilter::nearest : MipmapFilter::linear;
        require(model.textures[index].sampler ==
                    SamplerData{filter, filter, mip, expectedWraps[index % 3], expectedWraps[(index + 1) % 3]},
                "sampler filter or wrap conversion changed");
    }
    require(model.materials.size() == 1 && !model.images[0], "empty material import decoded an unused image");
}

template <typename Edit> void rejects(Edit edit, std::string_view expected) {
    auto asset = makeAsset();
    edit(asset);
    ModelData model;
    try {
        loadGltfMaterials(asset, {}, model);
    } catch (const std::runtime_error& error) {
        if (error.what() != expected) {
            throw std::runtime_error("Expected '" + std::string{expected} + "'; got: " + error.what());
        }
        return;
    }
    throw std::runtime_error("invalid material import was accepted");
}

void checkValidation() {
    rejects(
        [](auto& asset) {
            asset.textures[0].imageIndex.reset();
            asset.materials[0].ior = -1;
        },
        "Texture has no supported image source.");
    rejects(
        [](auto& asset) {
            asset.materials[0].ior = -1;
            asset.materials[0].pbrData.baseColorFactor[0] = -1;
        },
        "Invalid extension factor in material 'layered'");
    rejects(
        [](auto& asset) {
            asset.materials[0].specular->specularColorFactor[0] = -1;
        },
        "Invalid extension color in material 'layered'");
    rejects(
        [](auto& asset) {
            asset.materials[0].pbrData.baseColorFactor[0] = 2;
        },
        "Invalid base color factor in material layered");
    rejects(
        [](auto& asset) {
            asset.materials[0].alphaCutoff = -1;
        },
        "Invalid alpha cutoff.");
    rejects(
        [](auto& asset) {
            asset.materials[0].alphaMode = static_cast<fastgltf::AlphaMode>(255);
        },
        "Unknown glTF alpha mode.");
    rejects(
        [](auto& asset) {
            asset.images[0].data = std::monostate{};
            asset.materials[0].pbrData.roughnessFactor = -1;
        },
        "Invalid PBR scalar in material layered");
    rejects(
        [](auto& asset) {
            asset.materials[0].emissiveFactor[0] = 2;
        },
        "Invalid emissive factor in material layered");
    rejects(
        [](auto& asset) {
            asset.materials[0].pbrData.baseColorTexture->transform->rotation = std::numeric_limits<float>::infinity();
        },
        "Non-finite texture coordinate transform.");
    rejects(
        [](auto& asset) {
            asset.materials[0].sheen->sheenRoughnessTexture->texCoordIndex = 2;
        },
        "PBR textures support TEXCOORD_0 and TEXCOORD_1 only.");
    rejects(
        [](auto& asset) {
            asset.images[0].data = std::monostate{};
            asset.materials[0].normalTexture->texCoordIndex = 2;
        },
        "Material 'layered', texture 0, image 0: Unsupported image data source: red");
}

} // namespace

int main() {
    try {
        checkMaterials();
        checkSamplers();
        checkValidation();
        std::cout << "PASS glTF materials: surface/extensions, textures, image cache, samplers and validation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL glTF materials: " << error.what() << '\n';
        return 1;
    }
}
