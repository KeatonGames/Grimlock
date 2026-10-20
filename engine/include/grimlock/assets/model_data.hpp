#pragma once

#include "grimlock/assets/image_data.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace grimlock::assets {

struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 uv{};
    glm::vec2 uv1{};
    glm::vec4 tangent{}; // XYZ tangent, W handedness; W=0 disables normal mapping.
    glm::uvec4 joints0{}, joints1{};
    glm::vec4 weights0{}, weights1{};
    std::uint32_t morphIndex = 0; // Original accessor index; retained across normal/tangent seams.

    bool operator==(const Vertex& other) const noexcept {
        return position == other.position && normal == other.normal && uv == other.uv &&
               uv1 == other.uv1 && tangent == other.tangent && joints0 == other.joints0 &&
               joints1 == other.joints1 && weights0 == other.weights0 &&
               weights1 == other.weights1 && morphIndex == other.morphIndex;
    }
};

// One triangle-list primitive. A model node may reference several primitives.
struct MeshData {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t material = 0;

    struct MorphDelta {
        glm::vec4 position{}, normal{}, tangent{};
    };

    static_assert(sizeof(MorphDelta) == 48);
    std::vector<MorphDelta> morphDeltas; // Target-major; original accessor vertex order.
    std::uint32_t morphVertexCount = 0, morphTargetCount = 0;
};

enum class AlphaMode { opaque, mask, blend };
enum class TextureFilter { nearest, linear };
enum class MipmapFilter { none, nearest, linear };
enum class TextureWrap { repeat, mirroredRepeat, clampToEdge };

struct SamplerData {
    TextureFilter mag = TextureFilter::linear, min = TextureFilter::linear;
    MipmapFilter mip = MipmapFilter::linear;
    TextureWrap wrapU = TextureWrap::repeat, wrapV = TextureWrap::repeat;
    bool operator==(const SamplerData&) const = default;
};

struct TextureData {
    std::size_t image = 0;
    SamplerData sampler;
};

struct TextureReference {
    std::size_t texture = 0;
    std::uint32_t uvSet = 0;
    glm::vec2 offset{0.0F}, scale{1.0F};
    float rotation = 0.0F;
};

struct MaterialData {
    std::string name;
    glm::vec4 baseColor{1.0F};
    std::optional<TextureReference> baseColorTexture;
    AlphaMode alphaMode = AlphaMode::opaque;
    float alphaCutoff = 0.5F;
    bool doubleSided = false;
    // glTF metallic-roughness factors are linear; packed texture G=roughness, B=metallic.
    float metallic = 1.0F, roughness = 1.0F;
    glm::vec3 emissive{0.0F};
    std::optional<TextureReference> metallicRoughnessTexture, normalTexture, occlusionTexture,
        emissiveTexture;
    float normalScale = 1.0F, occlusionStrength = 1.0F;
    bool unlit = false;
    float emissiveStrength = 1.0F, ior = 1.5F, specular = 1.0F;
    glm::vec3 specularColor{1.0F};
    float clearcoat = 0, clearcoatRoughness = 0, clearcoatNormalScale = 1;
    std::optional<TextureReference> specularTexture, specularColorTexture, clearcoatTexture,
        clearcoatRoughnessTexture, clearcoatNormalTexture;
    // Preserved optional advanced layers. Rendering requires a future transmission/volume/sheen
    // path.
    float transmission = 0, thickness = 0,
          attenuationDistance = std::numeric_limits<float>::infinity();
    glm::vec3 attenuationColor{1}, sheenColor{0};
    float sheenRoughness = 0;
    std::optional<TextureReference> transmissionTexture, thicknessTexture, sheenColorTexture,
        sheenRoughnessTexture;
};

struct ModelNode {
    std::string name;
    std::optional<std::size_t> parent;
    std::vector<std::size_t> children;
    std::vector<std::size_t> meshes;
    // glTF TRS is converted to T * R * S; explicit matrices are kept unchanged.
    glm::mat4 localTransform{1.0F};
    glm::vec3 translation{0}, scale{1};
    glm::quat rotation{1, 0, 0, 0};
    bool matrixTransform = true;
    std::optional<std::size_t> skin, light;
    std::vector<float> weights;
    bool bindPosePreview =
        false; // Diagnosed legacy assets with missing rest-joint transforms only.
};

struct SkinData {
    std::string name;
    std::vector<std::size_t> joints;
    std::vector<glm::mat4> inverseBindMatrices;
    std::optional<std::size_t> skeleton;
};
enum class AnimationPath { translation, rotation, scale, weights };
enum class Interpolation { step, linear, cubicSpline };

struct AnimationSampler {
    std::vector<float> times, values; // glTF ordering; cubic = in/value/out per key.
    std::uint32_t components = 0;
    Interpolation interpolation = Interpolation::linear;
};

struct AnimationChannel {
    std::size_t sampler = 0, node = 0;
    AnimationPath path{};
};

struct AnimationData {
    std::string name;
    std::vector<AnimationSampler> samplers;
    std::vector<AnimationChannel> channels;
    float start = 0, end = 0;
};
enum class LightType { directional, point, spot };

struct LightData {
    std::string name;
    LightType type = LightType::point;
    glm::vec3 color{1};
    float intensity = 1, range = 0, innerCone = 0, outerCone = 0.785398163F;
};

struct ModelData {
    std::vector<MeshData> meshes;
    std::vector<ModelNode> nodes;
    std::vector<std::size_t> roots; // Selected scene only; other nodes remain preserved.
    std::vector<MaterialData> materials{MaterialData{}};
    // Images referenced by supported PBR channels are decoded once per source index.
    std::vector<std::optional<ImageData>> images;
    std::vector<TextureData> textures;
    std::vector<SkinData> skins;
    std::vector<AnimationData> animations;
    std::vector<LightData> lights;
    glm::vec3 minimum{};
    glm::vec3 maximum{};
    std::string warnings;
};

struct ModelNodeTransform {
    std::size_t node;
    glm::mat4 world;
};

// Validates the forest and returns selected-scene nodes in parent-first order.
[[nodiscard]] std::vector<ModelNodeTransform> modelNodeTransforms(const ModelData& model);
// Bounds include only selected-scene geometry, with imported node transforms applied.
void updateModelBounds(ModelData& model);

} // namespace grimlock::assets
