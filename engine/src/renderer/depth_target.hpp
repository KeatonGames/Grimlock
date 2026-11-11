#pragma once

#include "renderer/vulkan_context.hpp"

namespace grimlock::renderer {

class DepthTarget final {
public:
    DepthTarget(const VulkanContext& context,
                VkExtent2D extent,
                VkFormat format,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    ~DepthTarget();
    DepthTarget(const DepthTarget&) = delete;
    DepthTarget& operator=(const DepthTarget&) = delete;

    [[nodiscard]] static VkFormat chooseFormat(VkPhysicalDevice device, bool sampled = false);

    [[nodiscard]] VkImage image() const noexcept {
        return image_;
    }

    [[nodiscard]] VkImageView view() const noexcept {
        return view_;
    }

private:
    void destroy() noexcept;
    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
};

} // namespace grimlock::renderer
