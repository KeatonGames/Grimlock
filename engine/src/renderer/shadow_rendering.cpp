#include "renderer/vulkan_helpers.hpp"
#include "renderer/vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace grimlock::renderer {
void VulkanRenderer::detectShadowSupport() {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context_.physicalDevice(), &properties);
    try {
        shadowFormat_ = DepthTarget::chooseFormat(context_.physicalDevice(), true);
    } catch (const std::runtime_error&) {
        shadowFormat_ = VK_FORMAT_UNDEFINED;
    }
    if (shadowFormat_ != VK_FORMAT_UNDEFINED) {
        VkImageFormatProperties image{};
        const auto result = vkGetPhysicalDeviceImageFormatProperties(context_.physicalDevice(),
                                                                     shadowFormat_,
                                                                     VK_IMAGE_TYPE_2D,
                                                                     VK_IMAGE_TILING_OPTIMAL,
                                                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                                         VK_IMAGE_USAGE_SAMPLED_BIT,
                                                                     0,
                                                                     &image);
        if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
            shadowFormat_ = VK_FORMAT_UNDEFINED;
        } else {
            checkVk(result, "vkGetPhysicalDeviceImageFormatProperties(shadows)");
            maxShadowResolution_ =
                std::min({properties.limits.maxImageDimension2D, image.maxExtent.width, image.maxExtent.height});
        }
    }
    if (shadowFormat_ != VK_FORMAT_UNDEFINED) {
        VkImageFormatProperties image{};
        const auto result = vkGetPhysicalDeviceImageFormatProperties(
            context_.physicalDevice(),
            shadowFormat_,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            0,
            &image);
        if (result != VK_ERROR_FORMAT_NOT_SUPPORTED) {
            checkVk(result, "vkGetPhysicalDeviceImageFormatProperties(shadow readback)");
        }
        shadowReadback_ = result == VK_SUCCESS;
    }
    setShadowSettings(shadowSettings_);
}

std::vector<ShadowQuality> VulkanRenderer::supportedShadowQualities() const {
    std::vector<ShadowQuality> result{ShadowQuality::off};
    if (shadowFormat_ != VK_FORMAT_UNDEFINED) {
        for (auto quality : {ShadowQuality::low, ShadowQuality::medium, ShadowQuality::high}) {
            if (static_cast<std::uint32_t>(quality) <= maxShadowResolution_) {
                result.push_back(quality);
            }
        }
    }
    return result;
}

void VulkanRenderer::setShadowSettings(const ShadowSettings& settings) {
    validateShadowSettings(settings);
    auto normalized = settings;
    normalized.quality = ShadowQuality::off;
    for (auto quality : supportedShadowQualities()) {
        if (static_cast<std::uint32_t>(quality) <= static_cast<std::uint32_t>(settings.quality)) {
            normalized.quality = quality;
        }
    }
    shadowResourcesDirty_ = shadowResourcesDirty_ || normalized.quality != shadowSettings_.quality;
    shadowSettings_ = normalized;
}

void VulkanRenderer::updateShadowResources() {
    if (!shadowResourcesDirty_) {
        return;
    }
    // One frame is in flight; drawFrame has already waited for its fence. No new
    // queue/device wait is needed, and no descriptors/images change in a draw loop.
    shadowDepth_.reset();
    shadowInitialized_ = false;
    const auto size = static_cast<std::uint32_t>(shadowSettings_.quality);
    if (size) {
        shadowDepth_.emplace(context_,
                             VkExtent2D{size, size},
                             shadowFormat_,
                             VK_SAMPLE_COUNT_1_BIT,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                 (shadowReadback_ ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0));
        if (!shadowPipeline_) {
            shadowPipeline_.emplace(context_.device(),
                                    VK_FORMAT_UNDEFINED,
                                    shadowFormat_,
                                    cameraLayout_,
                                    materialLayout_,
                                    deformation_->layout(),
                                    false,
                                    VK_SAMPLE_COUNT_1_BIT,
                                    true);
        }
    }
    if (!shadowSampler_) {
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.minFilter = sampler.magFilter = VK_FILTER_NEAREST;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        checkVk(vkCreateSampler(context_.device(), &sampler, nullptr, &shadowSampler_), "vkCreateSampler(shadows)");
    }
    const VkDescriptorBufferInfo buffer{shadowBuffer_->handle(), 0, sizeof(ShadowUniforms)};
    const VkDescriptorImageInfo image{shadowSampler_,
                                      shadowDepth_ ? shadowDepth_->view() : whiteTexture_->view(),
                                      shadowDepth_ ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    std::array<VkWriteDescriptorSet, 2> writes{};
    for (std::size_t i = 0; i < writes.size(); ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = cameraSet_;
        writes[i].dstBinding = 5 + static_cast<std::uint32_t>(i);
        writes[i].descriptorCount = 1;
    }
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &buffer;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &image;
    vkUpdateDescriptorSets(context_.device(), 2, writes.data(), 0, nullptr);
    shadowResourcesDirty_ = false;
}

void VulkanRenderer::recordShadows(VkCommandBuffer command,
                                   const GridDraw* grid,
                                   std::span<const PrimitiveDraw> primitives) {
    if (!shadowDepth_) {
        return;
    }
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout =
        shadowInitialized_ ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = shadowDepth_->image();
    barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    if (!shadowDebug_.active) {
        if (!shadowInitialized_) {
            barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            vkCmdPipelineBarrier2(command, &dependency);
            shadowInitialized_ = true;
        }
        return;
    }
    vkCmdPipelineBarrier2(command, &dependency);
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = shadowDepth_->view();
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil.depth = 1;
    VkRenderingInfo pass{VK_STRUCTURE_TYPE_RENDERING_INFO};
    pass.renderArea.extent = {shadowDebug_.resolution, shadowDebug_.resolution};
    pass.layerCount = 1;
    pass.pDepthAttachment = &depth;
    vkCmdBeginRendering(command, &pass);
    const VkViewport viewport{0,
                              0,
                              static_cast<float>(shadowDebug_.resolution),
                              static_cast<float>(shadowDebug_.resolution),
                              0,
                              1};
    const VkRect2D scissor{{0, 0}, pass.renderArea.extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdSetDepthBias(command, shadowSettings_.constantBias, 0, shadowSettings_.slopeBias);
    vkCmdBindDescriptorSets(command,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            shadowPipeline_->layout(),
                            0,
                            1,
                            &cameraSet_,
                            0,
                            nullptr);
    if (grid) {
        if (!shadowGridPipeline_) {
            shadowGridPipeline_.emplace(context_.device(),
                                        VK_FORMAT_UNDEFINED,
                                        shadowFormat_,
                                        cameraLayout_,
                                        materialLayout_,
                                        deformation_->layout(),
                                        true,
                                        VK_SAMPLE_COUNT_1_BIT,
                                        true);
        }
        recordGrid(command, *grid, true);
    }
    recordPrimitiveDraws(command, primitives, true);
    vkCmdEndRendering(command);
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    vkCmdPipelineBarrier2(command, &dependency);
    shadowInitialized_ = true;
}

FrameCapture VulkanRenderer::captureShadowMap() {
    if (!shadowDepth_ || !shadowInitialized_ || !shadowDebug_.active || !shadowReadback_) {
        return {};
    }
    const auto size = shadowDebug_.resolution;
    const std::size_t pixels = static_cast<std::size_t>(size) * size;
    const std::size_t stride = shadowFormat_ == VK_FORMAT_D16_UNORM ? 2 : 4;
    Buffer readback{context_,
                    pixels * stride,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    // Tooling only: this ordered transfer/fence is never executed by the normal draw path.
    submitUpload(context_, commandPool_, [&](VkCommandBuffer command) {
        VkImageMemoryBarrier2 image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        image.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        image.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        image.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        image.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        image.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        image.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image.srcQueueFamilyIndex = image.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image.image = shadowDepth_->image();
        image.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &image;
        vkCmdPipelineBarrier2(command, &dependency);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        copy.imageExtent = {size, size, 1};
        vkCmdCopyImageToBuffer(command, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle(), 1, &copy);
        image.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        image.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        image.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        image.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        image.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkBufferMemoryBarrier2 host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        host.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.buffer = readback.handle();
        host.size = VK_WHOLE_SIZE;
        dependency.bufferMemoryBarrierCount = 1;
        dependency.pBufferMemoryBarriers = &host;
        vkCmdPipelineBarrier2(command, &dependency);
    });
    std::vector<std::uint8_t> raw(pixels * stride);
    readback.read(raw.data(), raw.size());
    const auto depthAt = [&](std::size_t index) {
        if (stride == 2) {
            std::uint16_t value;
            std::memcpy(&value, raw.data() + index * 2, 2);
            return value / 65535.0F;
        }
        float value;
        std::memcpy(&value, raw.data() + index * 4, 4);
        return std::isfinite(value) ? value : 1.0F;
    };
    float minimum = 1;
    for (std::size_t i = 0; i < pixels; ++i) {
        minimum = std::min(minimum, depthAt(i));
    }
    FrameCapture capture{size, size, std::vector<std::uint8_t>(pixels * 4)};
    for (std::size_t i = 0; i < pixels; ++i) {
        const auto value = static_cast<std::uint8_t>(
            std::clamp((depthAt(i) - minimum) / std::max(1 - minimum, 0.0001F), 0.0F, 1.0F) * 255);
        capture.rgba[i * 4] = capture.rgba[i * 4 + 1] = capture.rgba[i * 4 + 2] = value;
        capture.rgba[i * 4 + 3] = 255;
    }
    return capture;
}
} // namespace grimlock::renderer
