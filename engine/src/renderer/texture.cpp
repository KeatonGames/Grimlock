#include "renderer/texture.hpp"
#include "core/startup_profile.hpp"
#include "renderer/buffer.hpp"
#include "renderer/vulkan_helpers.hpp"

#include <algorithm>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <glm/gtc/packing.hpp>
#include <cmath>

namespace grimlock::renderer {

Texture::Texture(const VulkanContext& context, const VkCommandPool uploadPool, const assets::ImageData& data,
    const TextureColorSpace colorSpace) : device_(context.device()) {
    diagnostics::StartupSpan profile{"GPU.image.upload"};
    try {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context.physicalDevice(), &properties);
        if (data.width == 0 || data.height == 0 || data.width > properties.limits.maxImageDimension2D
            || data.height > properties.limits.maxImageDimension2D
            || data.pixels.size() != static_cast<std::size_t>(data.width) * data.height * 4) {
            throw std::invalid_argument("Invalid texture dimensions/pixels or image exceeds device limits: " + data.name);
        }
        const auto format = colorSpace == TextureColorSpace::srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        VkFormatProperties features{};
        vkGetPhysicalDeviceFormatProperties(context.physicalDevice(), format, &features);
        constexpr auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if ((features.optimalTilingFeatures & required) != required) { throw std::runtime_error("RGBA8 texture format is unsupported."); }
        linearFiltering_ = (features.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
        constexpr auto blit = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT
            | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((features.optimalTilingFeatures & blit) == blit) {
            mipLevels_ = std::bit_width(std::max(data.width, data.height));
        } else if (data.width > 1 || data.height > 1) {
            std::cerr << "Texture warning: linear blits unavailable; using one mip level for " << data.name << '\n';
        }
        auto usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (mipLevels_ > 1) { usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; }
        create(context, data.width, data.height, mipLevels_, 1, format, usage);
        Buffer staging{context, data.pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        staging.write(data.pixels.data(), data.pixels.size());
        submitUpload(context, uploadPool, [&](const VkCommandBuffer command) {
            const auto transition = [&](const std::uint32_t level, const std::uint32_t count, const VkImageLayout oldLayout,
                                        const VkImageLayout newLayout, const VkPipelineStageFlags2 srcStage,
                                        const VkAccessFlags2 srcAccess, const VkPipelineStageFlags2 dstStage,
                                        const VkAccessFlags2 dstAccess) {
                VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                barrier.srcStageMask = srcStage; barrier.srcAccessMask = srcAccess;
                barrier.dstStageMask = dstStage; barrier.dstAccessMask = dstAccess;
                barrier.oldLayout = oldLayout; barrier.newLayout = newLayout;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = image_;
                barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, count, 0, 1};
                VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                dependency.imageMemoryBarrierCount = 1; dependency.pImageMemoryBarriers = &barrier;
                vkCmdPipelineBarrier2(command, &dependency);
            };
            transition(0, mipLevels_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {data.width, data.height, 1};
            vkCmdCopyBufferToImage(command, staging.handle(), image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            auto width = static_cast<std::int32_t>(data.width), height = static_cast<std::int32_t>(data.height);
            for (std::uint32_t level = 1; level < mipLevels_; ++level) {
                transition(level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
                const auto nextWidth = std::max(1, width / 2), nextHeight = std::max(1, height / 2);
                VkImageBlit region{};
                region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
                region.srcOffsets[1] = {width, height, 1};
                region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
                region.dstOffsets[1] = {nextWidth, nextHeight, 1};
                vkCmdBlitImage(command, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &region, VK_FILTER_LINEAR);
                transition(level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                width = nextWidth; height = nextHeight;
            }
            transition(mipLevels_ - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        });
    } catch (...) { destroy(); throw; }
}

void Texture::create(const VulkanContext& context, const std::uint32_t width, const std::uint32_t height,
    const std::uint32_t levels, const std::uint32_t layers, const VkFormat format, const VkImageUsageFlags usage) {
    diagnostics::StartupSpan profile{"GPU.image.create"};
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context.physicalDevice(), &properties);
    if (width == 0 || height == 0 || width > properties.limits.maxImageDimension2D || height > properties.limits.maxImageDimension2D
        || (layers == 6 && (width != height || width > properties.limits.maxImageDimensionCube))
        || levels == 0 || levels > static_cast<std::uint32_t>(std::bit_width(std::max(width, height)))) { throw std::invalid_argument("Invalid Vulkan texture extent/mip levels."); }
    VkFormatProperties features{};
    vkGetPhysicalDeviceFormatProperties(context.physicalDevice(), format, &features);
    VkFormatFeatureFlags required = 0;
    if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0) { required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT; }
    if ((usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0) { required |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT; }
    if ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0) { required |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT; }
    if ((features.optimalTilingFeatures & required) != required) { throw std::runtime_error("Required sampled/HDR image format features are unavailable."); }
    linearFiltering_ = (features.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    mipLevels_ = levels;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = layers == 6 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    info.imageType = VK_IMAGE_TYPE_2D; info.format = format; info.extent = {width, height, 1};
    info.mipLevels = levels; info.arrayLayers = layers; info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateImage(device_, &info, nullptr, &image_), "vkCreateImage");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, image_, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = findMemoryType(context.physicalDevice(), requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    checkVk(vkAllocateMemory(device_, &allocation, nullptr, &memory_), "vkAllocateMemory(texture)");
    checkVk(vkBindImageMemory(device_, image_, memory_, 0), "vkBindImageMemory(texture)");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image_; view.viewType = layers == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D; view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, layers};
    checkVk(vkCreateImageView(device_, &view, nullptr, &view_), "vkCreateImageView(texture)");
}

Texture::Texture(const VulkanContext& context, const VkExtent2D extent, const VkFormat format, const VkImageUsageFlags usage)
    : device_(context.device()) {
    try { create(context, extent.width, extent.height, 1, 1, format, usage); }
    catch (...) { destroy(); throw; }
}

Texture::Texture(const VulkanContext& context, const VkCommandPool uploadPool,
    const std::span<const assets::FloatImageData> levels, const bool cube) : device_(context.device()) {
    diagnostics::StartupSpan profile{"GPU.image.upload"};
    try {
        const std::uint32_t layers = cube ? 6 : 1;
        if (levels.empty() || levels.size() % layers != 0 || levels.size() / layers > 32) { throw std::invalid_argument("Invalid prefiltered texture levels/faces."); }
        const auto mipCount = static_cast<std::uint32_t>(levels.size() / layers);
        create(context, levels[0].width, levels[0].height, mipCount, layers, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        if (!linearFiltering_) { throw std::runtime_error("HDR textures need linear filtering support."); }
        std::vector<std::uint16_t> pixels;
        std::vector<VkBufferImageCopy> copies;
        for (std::uint32_t mip = 0; mip < mipCount; ++mip) {
            for (std::uint32_t layer = 0; layer < layers; ++layer) {
                const auto& image = levels[mip * layers + layer];
                if (image.width != std::max(1U, levels[0].width >> mip) || image.height != std::max(1U, levels[0].height >> mip)
                    || image.pixels.size() != static_cast<std::size_t>(image.width) * image.height) { throw std::invalid_argument("Invalid precomputed mip dimensions."); }
                VkBufferImageCopy copy{};
                copy.bufferOffset = pixels.size() * sizeof(std::uint16_t);
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1}; copy.imageExtent = {image.width, image.height, 1};
                copies.push_back(copy);
                for (const auto& pixel : image.pixels) {
                    for (int c = 0; c < 4; ++c) {
                        if (!std::isfinite(pixel[c]) || pixel[c] < 0 || pixel[c] > 65504) { throw std::invalid_argument("HDR value is outside finite positive half-float range."); }
                        pixels.push_back(glm::packHalf1x16(pixel[c]));
                    }
                }
            }
        }
        Buffer staging{context, pixels.size() * sizeof(std::uint16_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        staging.write(pixels.data(), staging.size());
        submitUpload(context, uploadPool, [&](const VkCommandBuffer command) {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT; barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image_; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, layers};
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; dependency.imageMemoryBarrierCount = 1; dependency.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(command, &dependency);
            vkCmdCopyBufferToImage(command, staging.handle(), image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                static_cast<std::uint32_t>(copies.size()), copies.data());
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT; barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT; barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier2(command, &dependency);
        });
    } catch (...) { destroy(); throw; }
}

Texture::~Texture() { destroy(); }
Texture::Texture(Texture&& other) noexcept { *this = std::move(other); }
Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        destroy(); device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        image_ = std::exchange(other.image_, VK_NULL_HANDLE); memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
        view_ = std::exchange(other.view_, VK_NULL_HANDLE); mipLevels_ = other.mipLevels_; linearFiltering_ = other.linearFiltering_;
    }
    return *this;
}
void Texture::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, view_, nullptr); vkDestroyImage(device_, image_, nullptr); vkFreeMemory(device_, memory_, nullptr);
    }
    view_ = VK_NULL_HANDLE; image_ = VK_NULL_HANDLE; memory_ = VK_NULL_HANDLE;
}

} // namespace grimlock::renderer
