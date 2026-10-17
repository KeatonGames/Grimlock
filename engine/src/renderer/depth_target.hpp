#pragma once

#include "renderer/vulkan_context.hpp"

namespace grimlock::renderer {

class DepthTarget final {
public:
    DepthTarget(const VulkanContext& context, VkExtent2D extent, VkFormat format);
    ~DepthTarget();
    DepthTarget(const DepthTarget&) = delete;
    DepthTarget& operator=(const DepthTarget&) = delete;

    [[nodiscard]] static VkFormat chooseFormat(VkPhysicalDevice device);
    [[nodiscard]] VkImage image() const noexcept { return image_; }
    [[nodiscard]] VkImageView view() const noexcept { return view_; }

private:
    void destroy() noexcept;
    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
};

} // namespace grimlock::renderer
