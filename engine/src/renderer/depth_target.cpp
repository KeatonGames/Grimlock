#include "renderer/depth_target.hpp"

#include "renderer/vulkan_helpers.hpp"

#include <array>
#include <stdexcept>

namespace grimlock::renderer {

VkFormat DepthTarget::chooseFormat(const VkPhysicalDevice device) {
    constexpr std::array formats{VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM};

    for (const auto format : formats) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device, format, &properties);

        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return format;
        }
    }

    throw std::runtime_error("No supported depth attachment format (D32_SFLOAT or D16_UNORM).");
}

DepthTarget::DepthTarget(const VulkanContext& context, const VkExtent2D extent, const VkFormat format)
    : device_(context.device()) {
    try {
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = format;
        image.extent = {extent.width, extent.height, 1};
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        checkVk(vkCreateImage(device_, &image, nullptr, &image_), "vkCreateImage(depth)");

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, image_, &requirements);

        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex =
            findMemoryType(context.physicalDevice(), requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        checkVk(vkAllocateMemory(device_, &allocation, nullptr, &memory_), "vkAllocateMemory(depth)");
        checkVk(vkBindImageMemory(device_, image_, memory_, 0), "vkBindImageMemory(depth)");

        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = image_;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        checkVk(vkCreateImageView(device_, &view, nullptr, &view_), "vkCreateImageView(depth)");
    } catch (...) {
        destroy();
        throw;
    }
}

DepthTarget::~DepthTarget() {
    destroy();
}

void DepthTarget::destroy() noexcept {
    vkDestroyImageView(device_, view_, nullptr);
    vkDestroyImage(device_, image_, nullptr);
    vkFreeMemory(device_, memory_, nullptr);
    view_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
}

} // namespace grimlock::renderer
