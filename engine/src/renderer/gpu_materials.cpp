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

assets::SamplerData effectiveSampler(assets::SamplerData sampler,
                                     bool linearFiltering,
                                     const RenderSettings& settings) {
    if (!linearFiltering) {
        sampler.min = sampler.mag = assets::TextureFilter::nearest;
        sampler.mip = assets::MipmapFilter::nearest;
    } else if (sampler.min == assets::TextureFilter::linear && sampler.mag == assets::TextureFilter::linear) {
        // Nearest samplers often address palette atlases; retain their authored boundaries.
        sampler.mip = settings.textureFiltering == TextureFiltering::bilinear ? assets::MipmapFilter::nearest
                                                                              : assets::MipmapFilter::linear;
    }
    return sampler;
}

} // namespace

GpuMaterials::GpuMaterials(const VulkanContext& context,
                           const VkCommandPool uploadPool,
                           const VkDescriptorSetLayout layout,
                           const Texture& white,
                           const assets::ModelData& model,
                           const RenderSettings& settings)
    : device_(context.device()) {
    try {
        diagnostics::StartupSpan profile{"GPU.materials.upload"};

        if (model.materials.empty() || model.materials.size() > std::numeric_limits<std::uint32_t>::max() / 10) {
            throw std::invalid_argument("Model must have materials and fit descriptor limits.");
        }

        const auto count = static_cast<std::uint32_t>(model.materials.size());
        const std::array<VkDescriptorPoolSize, 2> sizes{
            {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count * 10}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count}}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = count;
        pool.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
        pool.pPoolSizes = sizes.data();
        checkVk(vkCreateDescriptorPool(device_, &pool, nullptr, &pool_), "vkCreateDescriptorPool(materials)");
        std::vector<VkDescriptorSetLayout> layouts(count, layout);
        std::vector<VkDescriptorSet> sets(count);

        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = pool_;
        allocate.descriptorSetCount = count;
        allocate.pSetLayouts = layouts.data();
        checkVk(vkAllocateDescriptorSets(device_, &allocate, sets.data()), "vkAllocateDescriptorSets(materials)");

        // An image used as both color and data legitimately needs both transfer functions.
        std::vector<std::array<std::optional<std::size_t>, 2>> uploadedImages(model.images.size());
        images_.reserve(model.images.size() * 2);
        uniforms_.reserve(count);
        bindings_.reserve(count);
        slots_.reserve(count * 10);
        constexpr std::array<std::uint32_t, 10> textureBindings{0, 2, 3, 4, 5, 6, 7, 8, 9, 10};

        for (std::size_t index = 0; index < model.materials.size(); ++index) {
            const auto& material = model.materials[index];
            MaterialUniforms data;
            data.baseColor = material.baseColor;
            data.emissiveRoughness = {material.emissive, material.roughness};
            data.factors = {material.metallic, material.normalScale, material.occlusionStrength, material.alphaCutoff};
            data.state = {static_cast<float>(material.alphaMode),
                          material.doubleSided ? 1.0F : 0.0F,
                          material.unlit ? 1.0F : 0.0F,
                          material.clearcoatNormalScale};
            data.specularColorIor = {material.specularColor, material.ior};
            data.extensions = {material.clearcoat,
                               material.clearcoatRoughness,
                               material.specular,
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
                        images_.emplace_back(context,
                                             uploadPool,
                                             *decoded,
                                             srgb ? TextureColorSpace::srgb : TextureColorSpace::linear);
                    }

                    image = &images_[*uploaded];
                    sampler = texture.sampler;
                    const auto c = std::cos(reference.rotation), s = std::sin(reference.rotation);
                    data.coordinates[slot].row0 = {c * reference.scale.x,
                                                   -s * reference.scale.y,
                                                   reference.offset.x,
                                                   static_cast<float>(reference.uvSet)};
                    data.coordinates[slot].row1 = {s * reference.scale.x, c * reference.scale.y, reference.offset.y, 1};
                }

                slots_.push_back(
                    {sets[index], textureBindings[slot], image->view(), sampler, image->linearFiltering()});
            }

            uniforms_.emplace_back(context,
                                   sizeof(data),
                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            uniforms_.back().write(&data, sizeof(data));

            VkWriteDescriptorSet write{};
            const VkDescriptorBufferInfo bufferInfo{uniforms_.back().handle(), 0, sizeof(data)};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = sets[index];
            write.dstBinding = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
            bindings_.push_back({sets[index], material.alphaMode, material.doubleSided});
        }
        setFiltering(context, settings);
    } catch (...) {
        destroy();
        throw;
    }
}

GpuMaterials::~GpuMaterials() {
    destroy();
}

void GpuMaterials::setFiltering(const VulkanContext& context, const RenderSettings& settings) {
    validateRenderSettings(settings);
    std::vector<VkSampler> replacements;
    std::vector<assets::SamplerData> descriptions;
    replacements.reserve(slots_.size());
    descriptions.reserve(slots_.size());
    try {
        for (const auto& slot : slots_) {
            const auto sampler = effectiveSampler(slot.sampler, slot.linearFiltering, settings);
            auto found = std::find(descriptions.begin(), descriptions.end(), sampler);

            if (found == descriptions.end()) {
                VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                info.magFilter = sampler.mag == assets::TextureFilter::linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
                info.minFilter = sampler.min == assets::TextureFilter::linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
                info.mipmapMode = sampler.mip == assets::MipmapFilter::linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                                              : VK_SAMPLER_MIPMAP_MODE_NEAREST;
                info.addressModeU = wrap(sampler.wrapU);
                info.addressModeV = wrap(sampler.wrapV);
                info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                info.maxLod = sampler.mip == assets::MipmapFilter::none ? 0 : VK_LOD_CLAMP_NONE;
                if (settings.textureFiltering == TextureFiltering::anisotropic && settings.anisotropy > 1 &&
                    context.maxAnisotropy() > 1 && info.minFilter == VK_FILTER_LINEAR &&
                    info.magFilter == VK_FILTER_LINEAR && sampler.mip != assets::MipmapFilter::none) {
                    info.anisotropyEnable = VK_TRUE;
                    info.maxAnisotropy = std::min(static_cast<float>(settings.anisotropy), context.maxAnisotropy());
                }
                VkSampler handle = VK_NULL_HANDLE;
                checkVk(vkCreateSampler(device_, &info, nullptr, &handle), "vkCreateSampler(material filtering)");
                replacements.push_back(handle);
                descriptions.push_back(sampler);
            }
        }
    } catch (...) {
        for (auto sampler : replacements) {
            vkDestroySampler(device_, sampler, nullptr);
        }
        throw;
    }
    // All allocations succeeded before replacing any live descriptor. The caller waited for GPU use.
    for (const auto& slot : slots_) {
        const auto sampler = effectiveSampler(slot.sampler, slot.linearFiltering, settings);
        const auto index = static_cast<std::size_t>(std::find(descriptions.begin(), descriptions.end(), sampler) -
                                                    descriptions.begin());
        const VkDescriptorImageInfo image{replacements.at(index), slot.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = slot.set;
        write.dstBinding = slot.binding;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
    for (auto sampler : samplers_) {
        vkDestroySampler(device_, sampler, nullptr);
    }
    samplers_ = std::move(replacements);
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
