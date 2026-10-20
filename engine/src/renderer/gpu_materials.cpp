#include "renderer/gpu_materials.hpp"

#include "core/startup_profile.hpp"
#include "renderer/vulkan_helpers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace grimlock::renderer {
namespace {
VkSamplerAddressMode wrap(const assets::TextureWrap value) {
    switch (value) {
    case assets::TextureWrap::repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case assets::TextureWrap::mirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case assets::TextureWrap::clampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    throw std::invalid_argument("Unknown texture wrap mode.");
}
} // namespace

GpuMaterials::GpuMaterials(const VulkanContext& context, const VkCommandPool uploadPool,
                           const VkDescriptorSetLayout layout, const Texture& white,
                           const assets::ModelData& model)
    : device_(context.device()) {
    try {
        diagnostics::StartupSpan profile{"GPU.materials.upload"};
        if (model.materials.empty() ||
            model.materials.size() > std::numeric_limits<std::uint32_t>::max() / 10) {
            throw std::invalid_argument("Model must have materials and fit descriptor limits.");
        }
        const auto count = static_cast<std::uint32_t>(model.materials.size());
        const std::array<VkDescriptorPoolSize, 2> sizes{
            {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count * 10},
             {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count}}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = count;
        pool.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
        pool.pPoolSizes = sizes.data();
        checkVk(vkCreateDescriptorPool(device_, &pool, nullptr, &pool_),
                "vkCreateDescriptorPool(materials)");
        std::vector<VkDescriptorSetLayout> layouts(count, layout);
        std::vector<VkDescriptorSet> sets(count);
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = pool_;
        allocate.descriptorSetCount = count;
        allocate.pSetLayouts = layouts.data();
        checkVk(vkAllocateDescriptorSets(device_, &allocate, sets.data()),
                "vkAllocateDescriptorSets(materials)");
        // An image used as both color and data legitimately needs both transfer functions.
        std::vector<std::array<std::optional<std::size_t>, 2>> uploadedImages(model.images.size());
        std::vector<assets::SamplerData> samplerDescriptions;
        images_.reserve(model.images.size() * 2);
        uniforms_.reserve(count);
        bindings_.reserve(count);
        samplers_.reserve(count * 5);
        const auto samplerFor = [&](const assets::SamplerData& sampler) {
            const auto found =
                std::find(samplerDescriptions.begin(), samplerDescriptions.end(), sampler);
            if (found != samplerDescriptions.end()) {
                return samplers_[static_cast<std::size_t>(found - samplerDescriptions.begin())];
            }
            VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            info.magFilter =
                sampler.mag == assets::TextureFilter::linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            info.minFilter =
                sampler.min == assets::TextureFilter::linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            info.mipmapMode = sampler.mip == assets::MipmapFilter::linear
                                  ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                  : VK_SAMPLER_MIPMAP_MODE_NEAREST;
            info.addressModeU = wrap(sampler.wrapU);
            info.addressModeV = wrap(sampler.wrapV);
            info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            info.maxLod = sampler.mip == assets::MipmapFilter::none ? 0.0F : VK_LOD_CLAMP_NONE;
            VkSampler handle = VK_NULL_HANDLE;
            checkVk(vkCreateSampler(device_, &info, nullptr, &handle), "vkCreateSampler");
            samplers_.push_back(handle);
            samplerDescriptions.push_back(sampler);
            return handle;
        };
        constexpr std::array<std::uint32_t, 10> textureBindings{0, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        for (std::size_t index = 0; index < model.materials.size(); ++index) {
            const auto& material = model.materials[index];
            MaterialUniforms data;
            data.baseColor = material.baseColor;
            data.emissiveRoughness = {material.emissive, material.roughness};
            data.factors = {material.metallic, material.normalScale, material.occlusionStrength,
                            material.alphaCutoff};
            data.state = {static_cast<float>(material.alphaMode),
                          material.doubleSided ? 1.0F : 0.0F, material.unlit ? 1.0F : 0.0F,
                          material.clearcoatNormalScale};
            data.specularColorIor = {material.specularColor, material.ior};
            data.extensions = {material.clearcoat, material.clearcoatRoughness, material.specular,
                               material.emissiveStrength};
            const std::array slots{&material.baseColorTexture,
                                   &material.metallicRoughnessTexture,
                                   &material.normalTexture,
                                   &material.occlusionTexture,
                                   &material.emissiveTexture,
                                   &material.specularTexture,
                                   &material.specularColorTexture,
                                   &material.clearcoatTexture,
                                   &material.clearcoatRoughnessTexture,
                                   &material.clearcoatNormalTexture};
            std::array<VkDescriptorImageInfo, 10> imageInfo{};
            std::array<VkWriteDescriptorSet, 11> writes{};
            for (std::size_t slot = 0; slot < slots.size(); ++slot) {
                const Texture* image = &white;
                assets::SamplerData sampler;
                if (*slots[slot]) {
                    const auto& reference = **slots[slot];
                    if (reference.uvSet > 1) {
                        throw std::invalid_argument("Unsupported material UV set.");
                    }
                    const auto& texture = model.textures.at(reference.texture);
                    const auto& decoded = model.images.at(texture.image);
                    if (!decoded) {
                        throw std::invalid_argument("Material references an undecoded image.");
                    }
                    const bool srgb = slot == 0 || slot == 4 || slot == 6;
                    auto& uploaded = uploadedImages[texture.image][srgb ? 1 : 0];
                    if (!uploaded) {
                        uploaded = images_.size();
                        images_.emplace_back(context, uploadPool, *decoded,
                                             srgb ? TextureColorSpace::srgb
                                                  : TextureColorSpace::linear);
                    }
                    image = &images_[*uploaded];
                    sampler = texture.sampler;
                    const auto c = std::cos(reference.rotation), s = std::sin(reference.rotation);
                    data.coordinates[slot].row0 = {c * reference.scale.x, -s * reference.scale.y,
                                                   reference.offset.x,
                                                   static_cast<float>(reference.uvSet)};
                    data.coordinates[slot].row1 = {s * reference.scale.x, c * reference.scale.y,
                                                   reference.offset.y, 1};
                }
                if (!image->linearFiltering() && (sampler.min == assets::TextureFilter::linear ||
                                                  sampler.mag == assets::TextureFilter::linear ||
                                                  sampler.mip == assets::MipmapFilter::linear)) {
                    throw std::runtime_error("Requested texture filtering is unsupported.");
                }
                imageInfo[slot] = {samplerFor(sampler), image->view(),
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                auto& write = writes[slot];
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = sets[index];
                write.dstBinding = textureBindings[slot];
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &imageInfo[slot];
            }
            uniforms_.emplace_back(context, sizeof(data), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            uniforms_.back().write(&data, sizeof(data));
            const VkDescriptorBufferInfo bufferInfo{uniforms_.back().handle(), 0, sizeof(data)};
            writes[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[10].dstSet = sets[index];
            writes[10].dstBinding = 1;
            writes[10].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[10].descriptorCount = 1;
            writes[10].pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()),
                                   writes.data(), 0, nullptr);
            bindings_.push_back({sets[index], material.alphaMode, material.doubleSided});
        }
    } catch (...) {
        destroy();
        throw;
    }
}

GpuMaterials::~GpuMaterials() {
    destroy();
}

void GpuMaterials::destroy() noexcept {
    vkDestroyDescriptorPool(device_, pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
    for (const auto sampler : samplers_) {
        vkDestroySampler(device_, sampler, nullptr);
    }
    samplers_.clear();
}

} // namespace grimlock::renderer
