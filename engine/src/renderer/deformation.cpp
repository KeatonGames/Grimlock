#include "renderer/deformation.hpp"

#include "renderer/vulkan_helpers.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace grimlock::renderer {

namespace {

constexpr auto host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

Deformation::Deformation(const VulkanContext& context)
    : context_(context), device_(context.device()),
      recordsBuffer_(context, initialDraws * sizeof(Record), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host),
      jointsBuffer_(context, initialJoints * sizeof(glm::mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host),
      weightsBuffer_(context, initialWeights * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host) {
    try {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context.physicalDevice(), &properties);
        storageLimit_ = properties.limits.maxStorageBufferRange;
        const auto countLimit = [&](std::size_t stride) {
            return static_cast<std::size_t>(
                std::min<VkDeviceSize>(storageLimit_ / stride, std::numeric_limits<std::uint32_t>::max()));
        };
        limits_ = {countLimit(sizeof(Record)), countLimit(sizeof(glm::mat4)), countLimit(sizeof(float))};

        if (properties.limits.maxStorageBufferRange < jointsBuffer_.size() ||
            properties.limits.maxPerStageDescriptorStorageBuffers < 4) {
            throw std::runtime_error("Device storage-buffer limits cannot support the deformation arena.");
        }

        std::array<VkDescriptorSetLayoutBinding, 4> bindings{};

        for (std::uint32_t i = 0; i < 4; ++i) {
            bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
        }

        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = 4;
        layout.pBindings = bindings.data();
        checkVk(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &layout_),
                "vkCreateDescriptorSetLayout(deformation)");
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxMeshSets * 4};

        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool.maxSets = maxMeshSets;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        checkVk(vkCreateDescriptorPool(device_, &pool, nullptr, &pool_), "vkCreateDescriptorPool(deformation)");
        records_.reserve(initialDraws);
        joints_.reserve(initialJoints);
        weights_.reserve(initialWeights);
        meshSets_.reserve(maxMeshSets);
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
    if (meshSets_.size() >= maxMeshSets) {
        throw std::length_error("Deformation descriptor pool exceeds 4096 unique GPU primitives.");
    }
    VkDescriptorSet set{};

    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = pool_;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &layout_;
    checkVk(vkAllocateDescriptorSets(device_, &allocation, &set),
            "vkAllocateDescriptorSets(deformation; maximum 4096 primitives)");
    // Reserved to the descriptor pool capacity, so registering an allocated set cannot grow the vector.
    meshSets_.push_back(set);
    const std::array<VkDescriptorBufferInfo, 4> buffers{{{recordsBuffer_.handle(), 0, recordsBuffer_.size()},
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
        (void)vkFreeDescriptorSets(device_, pool_, static_cast<std::uint32_t>(sets.size()), sets.data());
        std::erase_if(meshSets_, [&](VkDescriptorSet set) {
            return std::find(sets.begin(), sets.end(), set) != sets.end();
        });
    }
}

void Deformation::validateFrameSize(DeformationFrameSize size) const {
    const auto checkCount = [](std::size_t required, std::size_t maximum, const char* name) {
        if (required > maximum) {
            throw std::length_error("Deformation frame requires " + std::to_string(required) + " " + name +
                                    "; device storage-buffer limit is " + std::to_string(maximum) + ".");
        }
    };
    checkCount(size.draws, limits_.draws, "primitive draws");
    checkCount(size.joints, limits_.joints, "joint matrices");
    checkCount(size.weights, limits_.weights, "morph weights");
}

Deformation::NodeData Deformation::appendNode(std::span<const glm::mat4> joints, std::span<const float> weights) {
    if (joints.size() > limits_.joints || weights.size() > limits_.weights) {
        validateFrameSize({0, joints.size(), weights.size()});
    }
    validateFrameSize({records_.size(), joints_.size() + joints.size(), weights_.size() + weights.size()});
    const NodeData node{static_cast<std::uint32_t>(joints_.size()),
                        static_cast<std::uint32_t>(weights_.size()),
                        static_cast<std::uint32_t>(joints.size()),
                        static_cast<std::uint32_t>(weights.size())};
    joints_.insert(joints_.end(), joints.begin(), joints.end());
    weights_.insert(weights_.end(), weights.begin(), weights.end());
    return node;
}

std::uint32_t Deformation::append(NodeData node, std::uint32_t morphVertices, std::uint32_t morphTargets) {
    if (node.weightCount != morphTargets) {
        throw std::invalid_argument("Instance morph weights do not match primitive morph targets.");
    }
    if (node.jointOffset > joints_.size() || node.jointCount > joints_.size() - node.jointOffset ||
        node.weightOffset > weights_.size() || node.weightCount > weights_.size() - node.weightOffset) {
        throw std::invalid_argument("Deformation node data is outside the current frame arena.");
    }
    validateFrameSize({records_.size() + 1, joints_.size(), weights_.size()});

    const auto index = static_cast<std::uint32_t>(records_.size());
    records_.push_back(
        {{node.jointOffset, node.weightOffset, 0, 0}, {node.jointCount, morphTargets, morphVertices, 0}});

    return index;
}

void Deformation::refreshArenaBindings() {
    const std::array<VkDescriptorBufferInfo, 3> buffers{{{recordsBuffer_.handle(), 0, recordsBuffer_.size()},
                                                         {jointsBuffer_.handle(), 0, jointsBuffer_.size()},
                                                         {weightsBuffer_.handle(), 0, weightsBuffer_.size()}}};
    std::array<VkWriteDescriptorSet, 3> writes{};
    for (const auto set : meshSets_) {
        for (std::uint32_t i = 0; i < writes.size(); ++i) {
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &buffers[i];
        }
        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void Deformation::growBuffers() {
    const auto replacement = [&](const Buffer& buffer, VkDeviceSize required) -> std::optional<Buffer> {
        if (required <= buffer.size()) {
            return {};
        }
        const auto capacity = std::min(storageLimit_, std::max(required, buffer.size() * 2));
        return std::optional<Buffer>{std::in_place, context_, capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host};
    };
    // Allocate every replacement before changing live bindings. Failed allocation
    // leaves the old arena intact; successful growth is retained for later frames.
    auto records = replacement(recordsBuffer_, records_.size() * sizeof(Record));
    auto joints = replacement(jointsBuffer_, joints_.size() * sizeof(glm::mat4));
    auto weights = replacement(weightsBuffer_, weights_.size() * sizeof(float));
    const bool changed = records || joints || weights;
    if (records) {
        recordsBuffer_ = std::move(*records);
    }
    if (joints) {
        jointsBuffer_ = std::move(*joints);
    }
    if (weights) {
        weightsBuffer_ = std::move(*weights);
    }
    if (changed) {
        refreshArenaBindings();
    }
}

void Deformation::upload() {
    growBuffers();
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
