#pragma once

#include "grimlock/assets/model_data.hpp"
#include "renderer/buffer.hpp"
#include "renderer/texture.hpp"
#include <array>

namespace grimlock::renderer {

struct MaterialBinding {
    VkDescriptorSet set = VK_NULL_HANDLE;
    assets::AlphaMode alphaMode = assets::AlphaMode::opaque;
    bool doubleSided = false;
};

struct alignas(16) MaterialUniforms {
    glm::vec4 baseColor{1.0F};
    glm::vec4 emissiveRoughness{};
    glm::vec4 factors{}; // metallic, normal scale, occlusion strength, alpha cutoff
    glm::vec4 state{}; // alpha mode, double-sided
    glm::vec4 specularColorIor{1, 1, 1, 1.5F};
    glm::vec4 extensions{}; // clearcoat, coat roughness, specular weight, emissive strength
    struct TextureCoordinates {
        glm::vec4 row0{1, 0, 0, 0}; // affine row XYZ; W = UV set
        glm::vec4 row1{0, 1, 0, 0}; // affine row XYZ; W = texture present
    };
    std::array<TextureCoordinates, 10> coordinates{};
};
static_assert(sizeof(MaterialUniforms) == 416);
static_assert(offsetof(MaterialUniforms, coordinates) == 96);

// A model owns immutable material descriptors and shared images/samplers. All
// instances reference these sets; no descriptor updates occur during drawing.
class GpuMaterials final {
public:
    GpuMaterials(const VulkanContext& context, VkCommandPool uploadPool, VkDescriptorSetLayout layout,
        const Texture& white, const assets::ModelData& model);
    ~GpuMaterials();
    GpuMaterials(const GpuMaterials&) = delete;
    GpuMaterials& operator=(const GpuMaterials&) = delete;
    [[nodiscard]] const MaterialBinding& at(std::size_t index) const { return bindings_.at(index); }

private:
    void destroy() noexcept;
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<Texture> images_;
    std::vector<VkSampler> samplers_;
    std::vector<Buffer> uniforms_;
    std::vector<MaterialBinding> bindings_;
};

} // namespace grimlock::renderer
