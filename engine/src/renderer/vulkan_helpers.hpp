#pragma once

#include "renderer/vulkan_context.hpp"
#include "core/startup_profile.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace grimlock::renderer {

inline void checkVk(const VkResult result, const std::string_view operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string{operation} + " failed (VkResult "
            + std::to_string(static_cast<int>(result)) + ").");
    }
}

inline std::uint32_t findMemoryType(const VkPhysicalDevice device, const std::uint32_t allowed,
    const VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(device, &memory);
    for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
        if ((allowed & (1U << index)) != 0
            && (memory.memoryTypes[index].propertyFlags & required) == required) {
            return index;
        }
    }
    throw std::runtime_error("No compatible Vulkan memory type for the requested resource.");
}

// Shared one-shot upload path. The fence keeps staging resources alive until
// completion; record supplies resource-specific copies and Synchronization2 barriers.
template<typename Record>
void submitUpload(const VulkanContext& context, const VkCommandPool pool, Record&& record) {
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool submitted = false;
    try {
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        checkVk(vkAllocateCommandBuffers(context.device(), &allocation, &command), "vkAllocateCommandBuffers(upload)");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checkVk(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer(upload)");
        record(command);
        checkVk(vkEndCommandBuffer(command), "vkEndCommandBuffer(upload)");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        checkVk(vkCreateFence(context.device(), &fenceInfo, nullptr, &fence), "vkCreateFence(upload)");
        VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = command;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &commandInfo;
        checkVk(vkQueueSubmit2(context.graphicsQueue(), 1, &submit, fence), "vkQueueSubmit2(upload)");
        submitted = true;
        { diagnostics::StartupSpan profile{"wait.upload.fence"};
          checkVk(vkWaitForFences(context.device(), 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(upload)"); }
    } catch (...) {
        if (submitted) { vkDeviceWaitIdle(context.device()); }
        vkDestroyFence(context.device(), fence, nullptr);
        if (command != VK_NULL_HANDLE) { vkFreeCommandBuffers(context.device(), pool, 1, &command); }
        throw;
    }
    vkDestroyFence(context.device(), fence, nullptr);
    vkFreeCommandBuffers(context.device(), pool, 1, &command);
}

} // namespace grimlock::renderer
