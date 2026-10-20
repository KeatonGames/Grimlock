#include "assets/gltf_materials.hpp"

#include "assets/image_loader.hpp"
#include "core/startup_profile.hpp"
#include "grimlock/assets/model_data.hpp"

#include <cmath>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace grimlock::assets {

namespace {

TextureReference reference(const fastgltf::TextureInfo& source) {
    TextureReference result;
    result.texture = source.textureIndex;
    auto uvSet = source.texCoordIndex;

    if (source.transform) {
        const auto& transform = *source.transform;
        result.offset = {transform.uvOffset[0], transform.uvOffset[1]};
        result.scale = {transform.uvScale[0], transform.uvScale[1]};
        result.rotation = transform.rotation;

        if (transform.texCoordIndex) {
            uvSet = *transform.texCoordIndex;
        }
    }

    if (uvSet > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Texture UV index exceeds engine limits.");
    }

    result.uvSet = static_cast<std::uint32_t>(uvSet);

    if (!std::isfinite(result.offset.x) || !std::isfinite(result.offset.y) || !std::isfinite(result.scale.x) ||
        !std::isfinite(result.scale.y) || !std::isfinite(result.rotation)) {
        throw std::runtime_error("Non-finite texture coordinate transform.");
    }

    return result;
}

TextureWrap wrap(const fastgltf::Wrap value) {
    switch (value) {
    case fastgltf::Wrap::Repeat:
        return TextureWrap::repeat;
    case fastgltf::Wrap::MirroredRepeat:
        return TextureWrap::mirroredRepeat;
    case fastgltf::Wrap::ClampToEdge:
        return TextureWrap::clampToEdge;
    }

    throw std::runtime_error("Unsupported glTF sampler wrap mode.");
}

SamplerData sampler(const fastgltf::Sampler& source) {
    SamplerData result;
    result.wrapU = wrap(source.wrapS);
    result.wrapV = wrap(source.wrapT);

    if (source.magFilter) {
        switch (*source.magFilter) {
        case fastgltf::Filter::Nearest:
            result.mag = TextureFilter::nearest;
            break;
        case fastgltf::Filter::Linear:
            result.mag = TextureFilter::linear;
            break;
        default:
            throw std::runtime_error("Invalid glTF sampler magnification filter.");
        }
    }

    if (source.minFilter) {
        switch (*source.minFilter) {
        case fastgltf::Filter::Nearest:
            result.min = TextureFilter::nearest;
            result.mip = MipmapFilter::none;
            break;
        case fastgltf::Filter::Linear:
            result.min = TextureFilter::linear;
            result.mip = MipmapFilter::none;
            break;
        case fastgltf::Filter::NearestMipMapNearest:
            result.min = TextureFilter::nearest;
            result.mip = MipmapFilter::nearest;
            break;
        case fastgltf::Filter::LinearMipMapNearest:
            result.min = TextureFilter::linear;
            result.mip = MipmapFilter::nearest;
            break;
        case fastgltf::Filter::NearestMipMapLinear:
            result.min = TextureFilter::nearest;
            result.mip = MipmapFilter::linear;
            break;
        case fastgltf::Filter::LinearMipMapLinear:
            result.min = TextureFilter::linear;
            result.mip = MipmapFilter::linear;
            break;
        default:
            throw std::runtime_error("Invalid glTF sampler minification filter.");
        }
    }

    return result;
}

ImageData image(const fastgltf::Asset& asset, const std::size_t index, const std::filesystem::path& directory) {
    const auto& source = asset.images.at(index);
    const auto name = source.name.empty() ? "glTF image " + std::to_string(index) : std::string{source.name};

    return std::visit(
        [&](const auto& data) -> ImageData {
            using T = std::decay_t<decltype(data)>;

            if constexpr (std::is_same_v<T, fastgltf::sources::BufferView>) {
                if (data.bufferViewIndex >= asset.bufferViews.size()) {
                    throw std::runtime_error("Image buffer view is out of range.");
                }

                const auto bytes = fastgltf::DefaultBufferDataAdapter{}(asset, data.bufferViewIndex);

                return decodeImage({bytes.data(), bytes.size()}, name);
            } else if constexpr (std::is_same_v<T, fastgltf::sources::Array> ||
                                 std::is_same_v<T, fastgltf::sources::Vector> ||
                                 std::is_same_v<T, fastgltf::sources::ByteView>) {
                return decodeImage({data.bytes.data(), data.bytes.size()}, name);
            } else if constexpr (std::is_same_v<T, fastgltf::sources::URI>) {
                if (!data.uri.isLocalPath() || data.fileByteOffset != 0) {
                    throw std::runtime_error("Unsupported non-local image URI: " + name);
                }

                return loadImage(directory / data.uri.fspath());
            } else {
                throw std::runtime_error("Unsupported image data source: " + name);
            }
        },
        source.data);
}

} // namespace

void loadGltfMaterials(const fastgltf::Asset& asset, const std::filesystem::path& directory, ModelData& model) {
    diagnostics::StartupSpan profile{"asset.materials"};
    model.images.resize(asset.images.size());
    model.textures.reserve(asset.textures.size());

    for (const auto& texture : asset.textures) {
        if (!texture.imageIndex || *texture.imageIndex >= asset.images.size()) {
            throw std::runtime_error("Texture has no supported image source.");
        }

        TextureData data;
        data.image = *texture.imageIndex;

        if (texture.samplerIndex) {
            data.sampler = sampler(asset.samplers.at(*texture.samplerIndex));
        }

        model.textures.push_back(data);
    }

    model.materials.clear();

    for (const auto& source : asset.materials) {
        MaterialData material;
        material.name = source.name;
        material.unlit = source.unlit;
        material.emissiveStrength = source.emissiveStrength;
        material.ior = source.ior;

        if (source.specular) {
            material.specular = source.specular->specularFactor;
            const auto& color = source.specular->specularColorFactor;
            material.specularColor = {color[0], color[1], color[2]};

            if (source.specular->specularTexture) {
                material.specularTexture = reference(*source.specular->specularTexture);
            }

            if (source.specular->specularColorTexture) {
                material.specularColorTexture = reference(*source.specular->specularColorTexture);
            }
        }

        if (source.clearcoat) {
            material.clearcoat = source.clearcoat->clearcoatFactor;
            material.clearcoatRoughness = source.clearcoat->clearcoatRoughnessFactor;

            if (source.clearcoat->clearcoatTexture) {
                material.clearcoatTexture = reference(*source.clearcoat->clearcoatTexture);
            }

            if (source.clearcoat->clearcoatRoughnessTexture) {
                material.clearcoatRoughnessTexture = reference(*source.clearcoat->clearcoatRoughnessTexture);
            }

            if (source.clearcoat->clearcoatNormalTexture) {
                material.clearcoatNormalTexture = reference(*source.clearcoat->clearcoatNormalTexture);
                material.clearcoatNormalScale = source.clearcoat->clearcoatNormalTexture->scale;
            }
        }

        if (source.transmission) {
            material.transmission = source.transmission->transmissionFactor;

            if (source.transmission->transmissionTexture) {
                material.transmissionTexture = reference(*source.transmission->transmissionTexture);
            }
        }

        if (source.volume) {
            material.thickness = source.volume->thicknessFactor;
            material.attenuationDistance = source.volume->attenuationDistance;
            const auto& color = source.volume->attenuationColor;
            material.attenuationColor = {color[0], color[1], color[2]};

            if (source.volume->thicknessTexture) {
                material.thicknessTexture = reference(*source.volume->thicknessTexture);
            }
        }

        if (source.sheen) {
            const auto& color = source.sheen->sheenColorFactor;
            material.sheenColor = {color[0], color[1], color[2]};
            material.sheenRoughness = source.sheen->sheenRoughnessFactor;

            if (source.sheen->sheenColorTexture) {
                material.sheenColorTexture = reference(*source.sheen->sheenColorTexture);
            }

            if (source.sheen->sheenRoughnessTexture) {
                material.sheenRoughnessTexture = reference(*source.sheen->sheenRoughnessTexture);
            }
        }

        if (source.transmission || source.volume || source.sheen) {
            model.warnings += "Material '" + material.name +
                              "': transmission/volume/sheen data preserved but not rendered; core "
                              "surface preview only.\n";
        }

        const auto unit = [](float value) {
            return std::isfinite(value) && value >= 0 && value <= 1;
        };

        if (!unit(material.specular) || !unit(material.clearcoat) || !unit(material.clearcoatRoughness) ||
            !unit(material.transmission) || !unit(material.sheenRoughness) ||
            !std::isfinite(material.clearcoatNormalScale) || !std::isfinite(material.emissiveStrength) ||
            material.emissiveStrength < 0 || !std::isfinite(material.ior) || (material.ior != 0 && material.ior < 1) ||
            !std::isfinite(material.thickness) || material.thickness < 0 || std::isnan(material.attenuationDistance) ||
            material.attenuationDistance <= 0) {
            throw std::runtime_error("Invalid extension factor in material '" + material.name + "'");
        }

        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(material.specularColor[c]) || material.specularColor[c] < 0 ||
                !unit(material.sheenColor[c]) || !unit(material.attenuationColor[c])) {
                throw std::runtime_error("Invalid extension color in material '" + material.name + "'");
            }
        }

        for (int i = 0; i < 4; ++i) {
            material.baseColor[i] = source.pbrData.baseColorFactor[i];

            if (!std::isfinite(material.baseColor[i]) || material.baseColor[i] < 0.0F || material.baseColor[i] > 1.0F) {
                throw std::runtime_error("Invalid base color factor in material " + material.name);
            }
        }

        material.doubleSided = source.doubleSided;
        material.alphaCutoff = source.alphaCutoff;

        if (!std::isfinite(material.alphaCutoff) || material.alphaCutoff < 0.0F) {
            throw std::runtime_error("Invalid alpha cutoff.");
        }

        switch (source.alphaMode) {
        case fastgltf::AlphaMode::Opaque:
            material.alphaMode = AlphaMode::opaque;
            break;
        case fastgltf::AlphaMode::Mask:
            material.alphaMode = AlphaMode::mask;
            break;
        case fastgltf::AlphaMode::Blend:
            material.alphaMode = AlphaMode::blend;
            break;
        default:
            throw std::runtime_error("Unknown glTF alpha mode.");
        }

        if (source.pbrData.baseColorTexture) {
            material.baseColorTexture = reference(*source.pbrData.baseColorTexture);
        }

        material.metallic = source.pbrData.metallicFactor;
        material.roughness = source.pbrData.roughnessFactor;
        material.emissive = {source.emissiveFactor[0], source.emissiveFactor[1], source.emissiveFactor[2]};

        if (source.pbrData.metallicRoughnessTexture) {
            material.metallicRoughnessTexture = reference(*source.pbrData.metallicRoughnessTexture);
        }

        if (source.normalTexture) {
            material.normalTexture = reference(*source.normalTexture);
            material.normalScale = source.normalTexture->scale;
        }

        if (source.occlusionTexture) {
            material.occlusionTexture = reference(*source.occlusionTexture);
            material.occlusionStrength = source.occlusionTexture->strength;
        }

        if (source.emissiveTexture) {
            material.emissiveTexture = reference(*source.emissiveTexture);
        }

        if (!std::isfinite(material.metallic) || material.metallic < 0.0F || material.metallic > 1.0F ||
            !std::isfinite(material.roughness) || material.roughness < 0.0F || material.roughness > 1.0F ||
            !std::isfinite(material.normalScale) || !std::isfinite(material.occlusionStrength) ||
            material.occlusionStrength < 0.0F || material.occlusionStrength > 1.0F) {
            throw std::runtime_error("Invalid PBR scalar in material " + material.name);
        }

        for (int channel = 0; channel < 3; ++channel) {
            if (!std::isfinite(material.emissive[channel]) || material.emissive[channel] < 0.0F ||
                material.emissive[channel] > 1.0F) {
                throw std::runtime_error("Invalid emissive factor in material " + material.name);
            }
        }

        for (const auto* slot : {&material.baseColorTexture,
                                 &material.metallicRoughnessTexture,
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
            if (!*slot) {
                continue;
            }

            if ((*slot)->uvSet > 1) {
                throw std::runtime_error("PBR textures support TEXCOORD_0 and TEXCOORD_1 only.");
            }

            const auto imageIndex = model.textures.at((*slot)->texture).image;

            if (!model.images[imageIndex]) {
                try {
                    model.images[imageIndex] = image(asset, imageIndex, directory);
                } catch (const std::exception& error) {
                    throw std::runtime_error("Material '" + material.name + "', texture " +
                                             std::to_string((*slot)->texture) + ", image " +
                                             std::to_string(imageIndex) + ": " + error.what());
                }
            }
        }

        model.materials.push_back(std::move(material));
    }

    model.materials.push_back(MaterialData{}); // glTF default for unassigned primitives.
}

} // namespace grimlock::assets
