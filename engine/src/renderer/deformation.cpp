#include "renderer/deformation.hpp"

#include "renderer/vulkan_helpers.hpp"

#include <array>
#include <stdexcept>

namespace grimlock::renderer {
namespace {
constexpr auto host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

Deformation::Deformation(const VulkanContext& context)
    : device_(context.device()),
      recordsBuffer_(context, maxDraws * sizeof(Record), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host),
      jointsBuffer_(context, maxJoints * sizeof(glm::mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    host),
      weightsBuffer_(context, maxWeights * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     host) {
    try {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context.physicalDevice(), &properties);
        if (properties.limits.maxStorageBufferRange < jointsBuffer_.size() ||
            properties.limits.maxPerStageDescriptorStorageBuffers < 4) {
            throw std::runtime_error(
                "Device storage-buffer limits cannot support the deformation arena.");
        }
        std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
        for (std::uint32_t i = 0; i < 4; ++i) {
            bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,
                           nullptr};
        }
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = 4;
        layout.pBindings = bindings.data();
        checkVk(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &layout_),
                "vkCreateDescriptorSetLayout(deformation)");
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxDraws * 4};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool.maxSets = maxDraws;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        checkVk(vkCreateDescriptorPool(device_, &pool, nullptr, &pool_),
                "vkCreateDescriptorPool(deformation)");
        records_.reserve(maxDraws);
        joints_.reserve(maxJoints);
        weights_.reserve(maxWeights);
    } catch (...) {
        vkDestroyDescriptorPool(device_, pool_, nullptr);
        vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
        throw;
    }
}

Deformation::~Deformation() {
    vkDestroyDescriptorPool(device_, pool_, nullptr);
    vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
}

VkDescriptorSet Deformation::meshSet(VkBuffer morphs, VkDeviceSize size) {
    VkDescriptorSet set{};
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = pool_;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &layout_;
    checkVk(vkAllocateDescriptorSets(device_, &allocation, &set),
            "vkAllocateDescriptorSets(deformation; maximum 4096 primitives)");
    const std::array<VkDescriptorBufferInfo, 4> buffers{
        {{recordsBuffer_.handle(), 0, recordsBuffer_.size()},
         {jointsBuffer_.handle(), 0, jointsBuffer_.size()},
         {weightsBuffer_.handle(), 0, weightsBuffer_.size()},
         {morphs, 0, size}}};
    std::array<VkWriteDescriptorSet, 4> writes{};
    for (std::uint32_t i = 0; i < 4; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].descriptorCount = 1;
        writes[i].pBufferInfo = &buffers[i];
    }
    vkUpdateDescriptorSets(device_, 4, writes.data(), 0, nullptr);
    return set;
}

void Deformation::begin() {
    records_.clear();
    joints_.clear();
    weights_.clear();
}

void Deformation::releaseMeshSets(std::span<const VkDescriptorSet> sets) noexcept {
    if (!sets.empty()) {
        (void)vkFreeDescriptorSets(device_, pool_, static_cast<std::uint32_t>(sets.size()),
                                   sets.data());
    }
}

std::uint32_t Deformation::append(std::span<const glm::mat4> joints, std::span<const float> weights,
                                  std::uint32_t morphVertices, std::uint32_t morphTargets) {
    if (records_.size() >= maxDraws || joints.size() > maxJoints - joints_.size() ||
        weights.size() > maxWeights - weights_.size() || weights.size() != morphTargets) {
        throw std::runtime_error("Deformation frame exceeds 4096 draws/65536 joint matrices/65536 "
                                 "morph weights, or morph count mismatch.");
    }
    const auto index = static_cast<std::uint32_t>(records_.size());
    records_.push_back(
        {{static_cast<std::uint32_t>(joints_.size()), static_cast<std::uint32_t>(weights_.size()),
          0, 0},
         {static_cast<std::uint32_t>(joints.size()), morphTargets, morphVertices, 0}});
    joints_.insert(joints_.end(), joints.begin(), joints.end());
    weights_.insert(weights_.end(), weights.begin(), weights.end());
    return index;
}

void Deformation::upload() {
    if (!records_.empty()) {
        recordsBuffer_.write(records_.data(), records_.size() * sizeof(Record));
    }
    if (!joints_.empty()) {
        jointsBuffer_.write(joints_.data(), joints_.size() * sizeof(glm::mat4));
    }
    if (!weights_.empty()) {
        weightsBuffer_.write(weights_.data(), weights_.size() * sizeof(float));
    }
}
} // namespace grimlock::renderer
