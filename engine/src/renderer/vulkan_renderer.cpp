#include "renderer/vulkan_renderer.hpp"

#include "core/startup_profile.hpp"
#include "renderer/vulkan_helpers.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace grimlock::renderer {

namespace {

void validateModelDraw(const ModelDraw& draw) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(draw.transform[column][row])) {
                throw std::invalid_argument("Non-finite model transform.");
            }
        }

        if (!std::isfinite(draw.baseColor[column])) {
            throw std::invalid_argument("Non-finite model color.");
        }
    }
}

void validateNodeTransform(const glm::mat4& transform) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(transform[column][row])) {
                throw std::invalid_argument("Non-finite combined node transform.");
            }
        }
    }
}

} // namespace

VulkanRenderer::VulkanRenderer(SDL_Window* window, const assets::EnvironmentCube* environment)
    : window_(window), context_(window) {
    try {
        depthFormat_ = DepthTarget::chooseFormat(context_.physicalDevice());
        createCommandPool();
        deformation_.emplace(context_);
        createCameraResources(environment);
        createMaterialResources();
        framebufferResized_ = !hasDrawableExtent() || !createSwapchainResources();
        createSynchronization();
    } catch (...) {
        destroy();
        throw;
    }
}

VulkanRenderer::~VulkanRenderer() {
    destroy();
}

void VulkanRenderer::attachOverlay(RenderOverlay& overlay) {
    if (overlay_) {
        throw std::logic_error("A render overlay is already attached.");
    }

    if (!swapchain_ && !recreateSwapchain()) {
        throw std::runtime_error("Cannot attach an overlay to a zero-size surface.");
    }

    checkVk(vkDeviceWaitIdle(context_.device()), "vkDeviceWaitIdle(overlay attach)");

    try {
        overlay.initialize({window_,
                            context_.instance(),
                            context_.physicalDevice(),
                            context_.device(),
                            context_.graphicsQueue(),
                            context_.graphicsFamily(),
                            {swapchainFormat_, minimumImages_, static_cast<std::uint32_t>(swapchainImages_.size())}});
        overlay_ = &overlay;
    } catch (...) {
        overlay.shutdown();
        throw;
    }
}

void VulkanRenderer::detachOverlay() noexcept {
    if (!overlay_) {
        return;
    }

    (void)vkDeviceWaitIdle(context_.device());
    overlay_->shutdown();
    overlay_ = nullptr;
}

std::size_t VulkanRenderer::uploadModel(const assets::ModelData& model) {
    diagnostics::StartupSpan profile{"GPU.model.upload"};

    if (model.meshes.empty()) {
        throw std::invalid_argument("Cannot upload a model without meshes.");
    }

    auto owned = std::make_unique<GpuModel>();
    auto& uploaded = *owned;
    uploaded.deformationSets.reserve(model.meshes.size());

    try {
        // Evaluate while the caller's asset is alive; retain only the independent rest pose, not a
        // model/image copy.
        assets::ModelInstance rest{std::shared_ptr<const assets::ModelData>{&model, [](const auto*) {
                                                                            }}};
        uploaded.restPose = rest.pose();

        for (const auto& entry : assets::modelNodeTransforms(model)) {
            const auto& node = model.nodes[entry.node];

            if (!node.meshes.empty()) {
                uploaded.nodes.push_back({node.meshes, entry.node});
            }
        }

        if (uploaded.nodes.empty()) {
            throw std::invalid_argument("Cannot upload a model without selected-scene mesh nodes.");
        }

        uploaded.meshes.reserve(model.meshes.size());
        uploaded.materials =
            std::make_unique<GpuMaterials>(context_, commandPool_, materialLayout_, *whiteTexture_, model);
        for (const auto& mesh : model.meshes) {
            if (mesh.material >= model.materials.size()) {
                throw std::invalid_argument("Mesh material index is out of range.");
            }

            uploaded.meshes.emplace_back(context_, commandPool_, mesh);
            uploaded.deformationSets.push_back(
                deformation_->meshSet(uploaded.meshes.back().morphBuffer(), uploaded.meshes.back().morphSize()));
            uploaded.meshMaterials.push_back(mesh.material);
            glm::vec3 minimum{std::numeric_limits<float>::max()}, maximum{std::numeric_limits<float>::lowest()};

            for (const auto& vertex : mesh.vertices) {
                minimum = glm::min(minimum, vertex.position);
                maximum = glm::max(maximum, vertex.position);
            }

            uploaded.meshCenters.push_back(minimum + (maximum - minimum) * 0.5F);
        }

        const auto free = std::find(models_.begin(), models_.end(), nullptr);

        if (free != models_.end()) {
            *free = std::move(owned);

            return static_cast<std::size_t>(free - models_.begin());
        }

        models_.push_back(std::move(owned));

        return models_.size() - 1;
    } catch (...) {
        deformation_->releaseMeshSets(uploaded.deformationSets);
        throw;
    }
}

void VulkanRenderer::releaseModels(std::span<const std::size_t> models) noexcept {
    if (models.empty()) {
        return;
    }

    // Scene changes are infrequent. One safe lifecycle wait, not a wait per object.
    diagnostics::StartupSpan profile{"wait.scene_resource_release.device_idle"};
    (void)vkDeviceWaitIdle(context_.device());

    for (const auto id : models) {
        if (id >= models_.size() || !models_[id]) {
            continue;
        }

        deformation_->releaseMeshSets(models_[id]->deformationSets);
        models_[id].reset();
    }
}

bool VulkanRenderer::drawFrame(const Camera& camera, const std::span<const ModelDraw> draws, const Lighting& lighting) {
    diagnostics::StartupSpan profile{"frame.first_render"};

    if (!hasDrawableExtent()) {
        framebufferResized_ = true;

        return false;
    }

    if (framebufferResized_ && !recreateSwapchain()) {
        return false;
    }

    for (const auto& draw : draws) {
        if (draw.model >= models_.size() || !models_[draw.model]) {
            throw std::invalid_argument("Unknown GPU model ID.");
        }

        if (draw.materialOverride) {
            (void)models_[draw.model]->materials->at(*draw.materialOverride);
        }
    }

    const VkDevice device = context_.device();
    {
        diagnostics::StartupSpan waitProfile{"wait.frame.fence"};
        checkVk(vkWaitForFences(device, 1, &inFlight_, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    }

    const CameraUniforms uniforms{
        camera.view(),
        camera.projection(static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height)),
        glm::vec4{camera.position, 1}};
    cameraBuffer_->write(&uniforms, sizeof(uniforms));
    const auto lightData = lightingUniforms(lighting, specularEnvironment_->mipLevels());
    lightBuffer_->write(&lightData, sizeof(lightData));

    std::uint32_t imageIndex = 0;
    const VkResult acquire = [&] {
        diagnostics::StartupSpan waitProfile{"wait.frame.acquire"};

        return vkAcquireNextImageKHR(device, swapchain_, UINT64_MAX, imageAvailable_, VK_NULL_HANDLE, &imageIndex);
    }();

    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        framebufferResized_ = true;

        return false;
    }

    if (acquire != VK_SUBOPTIMAL_KHR) {
        checkVk(acquire, "vkAcquireNextImageKHR");
    }

    recordFrame(imageIndex, camera, draws, lighting.exposure);
    checkVk(vkResetFences(device, 1, &inFlight_), "vkResetFences");

    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = imageAvailable_;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = commandBuffers_.at(imageIndex);
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = renderFinished_.at(imageIndex);
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    checkVk(vkQueueSubmit2(context_.graphicsQueue(), 1, &submit, inFlight_), "vkQueueSubmit2");

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &signal.semaphore;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex;
    const VkResult result = [&] {
        diagnostics::StartupSpan waitProfile{"wait.frame.present_return"};

        return vkQueuePresentKHR(context_.presentQueue(), &present);
    }();

    if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR) {
        checkVk(result, "vkQueuePresentKHR");
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR) {
        framebufferResized_ = true;
        (void)recreateSwapchain();

        return result != VK_ERROR_OUT_OF_DATE_KHR;
    }

    return true;
}

void VulkanRenderer::requestSwapchainRecreation() noexcept {
    int width = 0, height = 0;

    // SDL queues initial/restored size events too. Preserve any existing invalidation,
    // but do not recreate a perfectly matching swapchain solely for a redundant event.
    if (!SDL_GetWindowSizeInPixels(window_, &width, &height) || width <= 0 || height <= 0 ||
        swapchain_ == VK_NULL_HANDLE || static_cast<std::uint32_t>(width) != swapchainExtent_.width ||
        static_cast<std::uint32_t>(height) != swapchainExtent_.height) {
        framebufferResized_ = true;
    }
}

void VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    info.queueFamilyIndex = context_.graphicsFamily();
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    checkVk(vkCreateCommandPool(context_.device(), &info, nullptr, &commandPool_), "vkCreateCommandPool");
}

void VulkanRenderer::createCameraResources(const assets::EnvironmentCube* environment) {
    const auto device = context_.device();
    cameraBuffer_.emplace(context_,
                          sizeof(CameraUniforms),
                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    lightBuffer_.emplace(context_,
                         sizeof(LightingUniforms),
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    const std::array<VkDescriptorSetLayoutBinding, 5> bindings{
        {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}}};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layout.pBindings = bindings.data();
    checkVk(vkCreateDescriptorSetLayout(device, &layout, nullptr, &cameraLayout_), "vkCreateDescriptorSetLayout");

    const VkDescriptorSetLayoutBinding toneMapBinding{0,
                                                      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                      1,
                                                      VK_SHADER_STAGE_FRAGMENT_BIT,
                                                      nullptr};
    layout.bindingCount = 1;
    layout.pBindings = &toneMapBinding;
    checkVk(vkCreateDescriptorSetLayout(device, &layout, nullptr, &toneMapLayout_),
            "vkCreateDescriptorSetLayout(tonemap)");

    const std::array<VkDescriptorPoolSize, 2> sizes{
        {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4}}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 2;
    pool.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    pool.pPoolSizes = sizes.data();
    checkVk(vkCreateDescriptorPool(device, &pool, nullptr, &descriptorPool_), "vkCreateDescriptorPool");

    const std::array layouts{cameraLayout_, toneMapLayout_};
    std::array<VkDescriptorSet, 2> sets{};

    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = descriptorPool_;
    allocation.descriptorSetCount = 2;
    allocation.pSetLayouts = layouts.data();
    checkVk(vkAllocateDescriptorSets(device, &allocation, sets.data()), "vkAllocateDescriptorSets");
    cameraSet_ = sets[0];
    toneMapSet_ = sets[1];

    const auto maps = environment ? assets::preprocessEnvironment(*environment)
                                  : assets::preprocessEnvironment(assets::fallbackEnvironment());
    irradiance_.emplace(context_, commandPool_, maps.irradiance, true);
    specularEnvironment_.emplace(context_, commandPool_, maps.specular, true);
    brdfLut_.emplace(context_, commandPool_, std::span{&maps.brdf, 1}, false);

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.minFilter = sampler.magFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    checkVk(vkCreateSampler(device, &sampler, nullptr, &environmentSampler_), "vkCreateSampler(environment)");

    const std::array<VkDescriptorBufferInfo, 2> buffers{
        {{cameraBuffer_->handle(), 0, sizeof(CameraUniforms)}, {lightBuffer_->handle(), 0, sizeof(LightingUniforms)}}};
    const std::array<VkDescriptorImageInfo, 3> images{
        {{environmentSampler_, irradiance_->view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
         {environmentSampler_, specularEnvironment_->view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
         {environmentSampler_, brdfLut_->view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
    std::array<VkWriteDescriptorSet, 5> writes{};

    for (std::size_t i = 0; i < writes.size(); ++i) {
        auto& write = writes[i];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = cameraSet_;
        write.dstBinding = static_cast<std::uint32_t>(i);
        write.descriptorCount = 1;

        if (i < 2) {
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &buffers[i];
        } else {
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &images[i - 2];
        }
    }

    vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void VulkanRenderer::createMaterialResources() {
    const std::array<VkDescriptorSetLayoutBinding, 11> bindings{
        {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}}};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layout.pBindings = bindings.data();
    checkVk(vkCreateDescriptorSetLayout(context_.device(), &layout, nullptr, &materialLayout_),
            "vkCreateDescriptorSetLayout(material)");

    const assets::ImageData white{"white fallback", 1, 1, {255, 255, 255, 255}};
    whiteTexture_.emplace(context_, commandPool_, white);
}

bool VulkanRenderer::createSwapchainResources() {
    if (!createSwapchain()) {
        return false;
    }

    createImageViews();
    depth_.emplace(context_, swapchainExtent_, depthFormat_);
    constexpr auto hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    hdrColor_.emplace(context_,
                      swapchainExtent_,
                      hdrFormat,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    const VkDescriptorImageInfo image{environmentSampler_, hdrColor_->view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = toneMapSet_;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(context_.device(), 1, &write, 0, nullptr);

    if (!pipeline_) {
        pipeline_.emplace(context_.device(),
                          hdrFormat,
                          depthFormat_,
                          cameraLayout_,
                          materialLayout_,
                          deformation_->layout());
    }

    if (!toneMapPipeline_ || toneMapPipeline_->colorFormat() != swapchainFormat_) {
        toneMapPipeline_.reset();
        toneMapPipeline_.emplace(context_.device(), swapchainFormat_, toneMapLayout_);
    }

    allocateCommandBuffers();

    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    renderFinished_.resize(swapchainImages_.size(), VK_NULL_HANDLE);

    for (auto& semaphore : renderFinished_) {
        checkVk(vkCreateSemaphore(context_.device(), &info, nullptr, &semaphore), "vkCreateSemaphore");
    }

    if (overlay_) {
        overlay_->targetChanged(
            {swapchainFormat_, minimumImages_, static_cast<std::uint32_t>(swapchainImages_.size())});
    }

    return true;
}

bool VulkanRenderer::createSwapchain() {
    diagnostics::StartupSpan profile{"swapchain.create"};
    const auto support = context_.swapchainSupport();

    if (support.formats.empty() || support.presentModes.empty() ||
        (support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("The surface does not support a presentable color attachment.");
    }

    const auto format = chooseSurfaceFormat(support.formats);
    const auto extent = chooseSwapchainExtent(support.capabilities);

    // Native surface changes can precede SDL's queued window events.
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }

    std::uint32_t count = support.capabilities.minImageCount + 1;
    minimumImages_ = std::max(2U, support.capabilities.minImageCount);

    if (support.capabilities.maxImageCount > 0) {
        count = std::min(count, support.capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = context_.surface();
    info.minImageCount = count;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const std::array families{context_.graphicsFamily(), context_.presentFamily()};

    if (families[0] != families[1]) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = static_cast<std::uint32_t>(families.size());
        info.pQueueFamilyIndices = families.data();
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    info.preTransform = support.capabilities.currentTransform;
    constexpr std::array alphaModes{VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                                    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
    for (const auto mode : alphaModes) {
        if ((support.capabilities.supportedCompositeAlpha & mode) != 0) {
            info.compositeAlpha = mode;
            break;
        }
    }

    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    checkVk(vkCreateSwapchainKHR(context_.device(), &info, nullptr, &swapchain_), "vkCreateSwapchainKHR");
    checkVk(vkGetSwapchainImagesKHR(context_.device(), swapchain_, &count, nullptr), "vkGetSwapchainImagesKHR");
    swapchainImages_.resize(count);
    checkVk(vkGetSwapchainImagesKHR(context_.device(), swapchain_, &count, swapchainImages_.data()),
            "vkGetSwapchainImagesKHR");
    swapchainFormat_ = format.format;
    swapchainExtent_ = extent;

    return true;
}

void VulkanRenderer::createImageViews() {
    swapchainImageViews_.resize(swapchainImages_.size());

    for (std::size_t index = 0; index < swapchainImages_.size(); ++index) {
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        info.image = swapchainImages_[index];
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = swapchainFormat_;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.layerCount = 1;
        checkVk(vkCreateImageView(context_.device(), &info, nullptr, &swapchainImageViews_[index]),
                "vkCreateImageView");
    }
}

void VulkanRenderer::allocateCommandBuffers() {
    std::vector<VkCommandBuffer> buffers(swapchainImages_.size());

    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = commandPool_;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = static_cast<std::uint32_t>(buffers.size());
    checkVk(vkAllocateCommandBuffers(context_.device(), &allocation, buffers.data()), "vkAllocateCommandBuffers");
    commandBuffers_.swap(buffers);
}

std::vector<VulkanRenderer::PrimitiveDraw> VulkanRenderer::preparePrimitiveDraws(
    const Camera& camera,
    const std::span<const ModelDraw> draws) {
    std::vector<PrimitiveDraw> primitives;
    deformation_->begin();
    const auto view = camera.view();

    for (const auto& draw : draws) {
        validateModelDraw(draw);

        const auto& model = *models_.at(draw.model);
        const auto& pose = draw.pose ? *draw.pose : model.restPose;

        if (pose.world.size() != model.restPose.world.size() || pose.joints.size() != pose.world.size() ||
            pose.weights.size() != pose.world.size()) {
            throw std::invalid_argument("Instance pose does not match the GPU model.");
        }

        for (const auto& node : model.nodes) {
            if (pose.joints.at(node.node).size() != model.restPose.joints[node.node].size()) {
                throw std::invalid_argument("Instance joint palette does not match model skin.");
            }

            const glm::mat4 transform = draw.transform * pose.world.at(node.node);

            validateNodeTransform(transform);

            const glm::mat3 linear{transform};
            const float determinant = glm::determinant(linear);

            if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12F) {
                throw std::invalid_argument("Model/node transform must have a finite, invertible linear part.");
            }

            const glm::mat3 normal = glm::transpose(glm::inverse(linear));
            const DrawConstants constants{transform,
                                          glm::vec4{normal[0], determinant < 0.0F ? -1.0F : 1.0F},
                                          glm::vec4{normal[1], draw.selected ? 1.0F : 0.0F},
                                          glm::vec4{normal[2], 0.0F},
                                          draw.baseColor};
            for (const auto mesh : node.meshes) {
                const auto& material = model.materials->at(draw.materialOverride.value_or(model.meshMaterials[mesh]));
                const float depth = -(view * transform * glm::vec4{model.meshCenters[mesh], 1.0F}).z;

                if (!std::isfinite(depth)) {
                    throw std::invalid_argument("Non-finite primitive sort depth.");
                }

                const auto& geometry = model.meshes[mesh];
                const auto deformation = deformation_->append(pose.joints[node.node],
                                                              pose.weights[node.node],
                                                              geometry.morphVertexCount,
                                                              geometry.morphTargetCount);
                primitives.push_back(
                    {&model, mesh, constants, &material, determinant < 0.0F, depth, primitives.size(), deformation});
            }
        }
    }

    return primitives;
}

void VulkanRenderer::sortPrimitiveDraws(std::span<PrimitiveDraw> primitives) {
    // OPAQUE/MASK write depth first. BLEND tests depth without writing it, sorted
    // back-to-front across every model/node/instance by view-space primitive center.
    std::sort(primitives.begin(), primitives.end(), [](const auto& a, const auto& b) {
        const bool aBlend = a.material->alphaMode == assets::AlphaMode::blend;
        const bool bBlend = b.material->alphaMode == assets::AlphaMode::blend;

        if (aBlend != bBlend) {
            return !aBlend;
        }

        if (aBlend && a.depth != b.depth) {
            return a.depth > b.depth;
        }

        return a.order < b.order;
    });
}

void VulkanRenderer::recordPrimitiveDraws(VkCommandBuffer command, std::span<const PrimitiveDraw> primitives) {
    VkPipeline currentPipeline = VK_NULL_HANDLE;
    deformation_->upload();
    VkDescriptorSet currentMaterial = VK_NULL_HANDLE;

    for (const auto& primitive : primitives) {
        const auto& material = *primitive.material;
        const auto pipeline =
            pipeline_->handle(material.alphaMode == assets::AlphaMode::blend, material.doubleSided, primitive.mirrored);
        if (pipeline != currentPipeline) {
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            currentPipeline = pipeline;
        }

        if (material.set != currentMaterial) {
            vkCmdBindDescriptorSets(command,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_->layout(),
                                    1,
                                    1,
                                    &material.set,
                                    0,
                                    nullptr);
            currentMaterial = material.set;
        }

        vkCmdPushConstants(command,
                           pipeline_->layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(primitive.constants),
                           &primitive.constants);
        const auto set = primitive.model->deformationSets[primitive.mesh];
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(), 2, 1, &set, 0, nullptr);
        primitive.model->meshes[primitive.mesh].draw(command, primitive.deformation);
    }
}

void VulkanRenderer::recordFrame(const std::uint32_t imageIndex,
                                 const Camera& camera,
                                 const std::span<const ModelDraw> draws,
                                 const float exposure) {
    const auto command = commandBuffers_.at(imageIndex);
    checkVk(vkResetCommandBuffer(command, 0), "vkResetCommandBuffer");

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");

    std::array<VkImageMemoryBarrier2, 2> barriers{};
    auto& color = barriers[0];
    color.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    color.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    color.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    color.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    color.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    color.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    color.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    color.image = hdrColor_->image();
    color.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    color.subresourceRange.levelCount = 1;
    color.subresourceRange.layerCount = 1;

    auto& depthBarrier = barriers[1];
    depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    depthBarrier.srcStageMask =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    depthBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.dstStageMask = depthBarrier.srcStageMask;
    depthBarrier.dstAccessMask =
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.image = depth_->image();
    depthBarrier.subresourceRange = color.subresourceRange;
    depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
    dependency.pImageMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(command, &dependency);

    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = hdrColor_->view();
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = {{0.025F, 0.055F, 0.105F, 1.0F}};

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = depth_->view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil.depth = 1.0F;

    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = swapchainExtent_;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    rendering.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(command, &rendering);

    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchainExtent_.width);
    viewport.height = static_cast<float>(swapchainExtent_.height);
    viewport.maxDepth = 1.0F;
    const VkRect2D scissor{{0, 0}, swapchainExtent_};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindDescriptorSets(command,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_->layout(),
                            0,
                            1,
                            &cameraSet_,
                            0,
                            nullptr);

    auto primitives = preparePrimitiveDraws(camera, draws);
    sortPrimitiveDraws(primitives);
    recordPrimitiveDraws(command, primitives);

    vkCmdEndRendering(command);

    VkImageMemoryBarrier2 sampled = color;
    sampled.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    sampled.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    sampled.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    sampled.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    sampled.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    sampled.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkImageMemoryBarrier2 acquired = color;

    // Match the acquire-semaphore wait stage before touching the swapchain image.
    acquired.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    acquired.srcAccessMask = VK_ACCESS_2_NONE;
    acquired.image = swapchainImages_.at(imageIndex);
    const std::array transitions{sampled, acquired};
    dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(transitions.size());
    dependency.pImageMemoryBarriers = transitions.data();
    vkCmdPipelineBarrier2(command, &dependency);

    attachment.imageView = swapchainImageViews_.at(imageIndex);
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    rendering.pDepthAttachment = nullptr;
    vkCmdBeginRendering(command, &rendering);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, toneMapPipeline_->handle());
    vkCmdBindDescriptorSets(command,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            toneMapPipeline_->layout(),
                            0,
                            1,
                            &toneMapSet_,
                            0,
                            nullptr);
    vkCmdPushConstants(command,
                       toneMapPipeline_->layout(),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(exposure),
                       &exposure);
    vkCmdDraw(command, 3, 1, 0, 0);

    if (overlay_) {
        overlay_->render(command);
    }

    vkCmdEndRendering(command);

    VkImageMemoryBarrier2 present = acquired;
    present.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    present.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    present.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    present.dstAccessMask = VK_ACCESS_2_NONE;
    present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &present;
    vkCmdPipelineBarrier2(command, &dependency);
    checkVk(vkEndCommandBuffer(command), "vkEndCommandBuffer");
}

void VulkanRenderer::createSynchronization() {
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    checkVk(vkCreateSemaphore(context_.device(), &semaphore, nullptr, &imageAvailable_), "vkCreateSemaphore");
    checkVk(vkCreateFence(context_.device(), &fence, nullptr, &inFlight_), "vkCreateFence");
}

bool VulkanRenderer::recreateSwapchain() {
    if (!hasDrawableExtent()) {
        return false;
    }

    {
        diagnostics::StartupSpan waitProfile{"wait.device.idle_recreate"};
        checkVk(vkDeviceWaitIdle(context_.device()), "vkDeviceWaitIdle");
    }

    destroySwapchainResources();

    if (!createSwapchainResources()) {
        return false;
    }

    framebufferResized_ = false;

    return true;
}

VkSurfaceFormatKHR VulkanRenderer::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const {
    if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED) {
        return {VK_FORMAT_B8G8R8A8_SRGB, formats.front().colorSpace};
    }

    for (const auto desired : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB}) {
        const auto preferred =
            std::find_if(formats.begin(), formats.end(), [desired](const VkSurfaceFormatKHR& format) {
                return format.format == desired && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            });
        if (preferred != formats.end()) {
            return *preferred;
        }
    }

    throw std::runtime_error("Surface lacks an sRGB presentation format required for correct "
                             "linear-light shading and blending.");
}

VkExtent2D VulkanRenderer::chooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width = 0, height = 0;

    if (!SDL_GetWindowSizeInPixels(window_, &width, &height)) {
        throw std::runtime_error(std::string{"SDL_GetWindowSizeInPixels failed: "} + SDL_GetError());
    }

    return {std::clamp(static_cast<std::uint32_t>(std::max(width, 0)),
                       capabilities.minImageExtent.width,
                       capabilities.maxImageExtent.width),
            std::clamp(static_cast<std::uint32_t>(std::max(height, 0)),
                       capabilities.minImageExtent.height,
                       capabilities.maxImageExtent.height)};
}

bool VulkanRenderer::hasDrawableExtent() const {
    if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0) {
        return false;
    }

    int width = 0, height = 0;

    if (!SDL_GetWindowSizeInPixels(window_, &width, &height)) {
        throw std::runtime_error(std::string{"SDL_GetWindowSizeInPixels failed: "} + SDL_GetError());
    }

    return width > 0 && height > 0;
}

void VulkanRenderer::destroySwapchainResources() noexcept {
    const auto device = context_.device();

    for (const auto semaphore : renderFinished_) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }

    renderFinished_.clear();

    if (!commandBuffers_.empty()) {
        vkFreeCommandBuffers(device,
                             commandPool_,
                             static_cast<std::uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
        commandBuffers_.clear();
    }

    depth_.reset();
    hdrColor_.reset();

    for (const auto view : swapchainImageViews_) {
        vkDestroyImageView(device, view, nullptr);
    }

    swapchainImageViews_.clear();
    swapchainImages_.clear();
    vkDestroySwapchainKHR(device, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void VulkanRenderer::destroy() noexcept {
    const auto device = context_.device();
    vkDeviceWaitIdle(device);
    detachOverlay();

    destroySwapchainResources();
    pipeline_.reset();
    toneMapPipeline_.reset();
    models_.clear();
    deformation_.reset();
    whiteTexture_.reset();

    vkDestroyDescriptorSetLayout(device, materialLayout_, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
    vkDestroyDescriptorSetLayout(device, cameraLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device, toneMapLayout_, nullptr);
    vkDestroySampler(device, environmentSampler_, nullptr);

    irradiance_.reset();
    specularEnvironment_.reset();
    brdfLut_.reset();
    lightBuffer_.reset();
    cameraBuffer_.reset();

    vkDestroyFence(device, inFlight_, nullptr);
    vkDestroySemaphore(device, imageAvailable_, nullptr);
    vkDestroyCommandPool(device, commandPool_, nullptr);

    descriptorPool_ = VK_NULL_HANDLE;
    cameraLayout_ = VK_NULL_HANDLE;
    materialLayout_ = VK_NULL_HANDLE;
    toneMapLayout_ = VK_NULL_HANDLE;
    environmentSampler_ = VK_NULL_HANDLE;
    inFlight_ = VK_NULL_HANDLE;
    imageAvailable_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
}

} // namespace grimlock::renderer
