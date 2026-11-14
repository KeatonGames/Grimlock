#pragma once
#include "assets/environment.hpp"
#include "grimlock/assets/animation.hpp"
#include "grimlock/camera.hpp"
#include "grimlock/engine.hpp"
#include "grimlock/render_overlay.hpp"
#include "renderer/buffer.hpp"
#include "renderer/deformation.hpp"
#include "renderer/depth_target.hpp"
#include "renderer/gpu_materials.hpp"
#include "renderer/gpu_mesh.hpp"
#include "renderer/grid_instances.hpp"
#include "renderer/lighting.hpp"
#include "renderer/mesh_pipeline.hpp"
#include "renderer/shadow_camera.hpp"
#include "renderer/tone_map_pipeline.hpp"
#include "renderer/vulkan_context.hpp"

#include <cstddef>
#include <grimlock/performance.hpp>
#include <memory>
#include <optional>
#include <span>
#include <vector>

struct SDL_Window;

namespace grimlock::renderer {

struct ModelDraw {
    std::size_t model = 0;
    glm::mat4 transform{1.0F};
    glm::vec4 baseColor{1.0F};                   // Optional linear-space instance tint, neutral by default.
    std::optional<std::size_t> materialOverride; // Share geometry in the PBR comparison scene.
    const assets::ModelPose* pose = nullptr;     // Instance-owned; immutable for the duration of drawFrame.
    bool selected = false;
    float dissolve = 0;
    ShadowProperties shadows;
};

struct GridDraw {
    std::size_t cubeModel;
    const Grid& grid;
};

class VulkanRenderer final {
public:
    explicit VulkanRenderer(SDL_Window* window, const assets::EnvironmentCube* environment = nullptr);
    ~VulkanRenderer();
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    // Internal IDs remain valid until releaseModels; freed slots may be reused.
    [[nodiscard]] std::size_t uploadModel(const assets::ModelData& model);
    void releaseModels(std::span<const std::size_t> models) noexcept;
    void validateFrameSize(DeformationFrameSize size) const;
    [[nodiscard]] bool drawFrame(const Camera& camera,
                                 std::span<const ModelDraw> draws,
                                 const Lighting& lighting = {},
                                 const GridDraw* grid = nullptr);
    void invalidateGrid() noexcept;
    void requestSwapchainRecreation() noexcept;
    void attachOverlay(RenderOverlay& overlay);
    void detachOverlay() noexcept;
    void requestCapture();
    [[nodiscard]] FrameCapture takeCapture();
    void setVsync(bool enabled);
    void setMsaaSamples(std::uint32_t samples);
    void setRenderSettings(const RenderSettings& settings);

    [[nodiscard]] RenderSettings renderSettings() const noexcept {
        return renderSettings_;
    }

    [[nodiscard]] std::vector<std::uint32_t> supportedAnisotropy() const;
    void setShadowSettings(const ShadowSettings& settings);

    [[nodiscard]] ShadowSettings shadowSettings() const noexcept {
        return shadowSettings_;
    }

    [[nodiscard]] std::vector<ShadowQuality> supportedShadowQualities() const;

    [[nodiscard]] ShadowDebugInfo shadowDebugInfo() const noexcept {
        return shadowDebug_;
    }

    [[nodiscard]] FrameCapture captureShadowMap();

    [[nodiscard]] std::vector<std::uint32_t> supportedMsaaSamples() const;

    [[nodiscard]] std::uint32_t msaaSamples() const noexcept {
        return samples_;
    }

    [[nodiscard]] glm::uvec2 renderingResolution() const noexcept {
        return {swapchainExtent_.width, swapchainExtent_.height};
    }

    [[nodiscard]] bool vsyncEnabled() const noexcept {
        return vsync_;
    }

    void setPerformanceEnabled(bool enabled) noexcept {
        performanceEnabled_ = enabled;
    }

    [[nodiscard]] const FramePerformance& performance() const noexcept {
        return performance_;
    }

private:
    struct PrimitiveDraw;
    void createCommandPool();
    void detectSampleCounts();
    void detectShadowSupport();
    void updateShadowResources();
    void recordShadows(VkCommandBuffer command, const GridDraw* grid, std::span<const PrimitiveDraw> primitives);
    void createCameraResources(const assets::EnvironmentCube* environment);
    void createMaterialResources();
    void createSynchronization();
    [[nodiscard]] bool createSwapchainResources();
    [[nodiscard]] bool createSwapchain();
    void createImageViews();
    void allocateCommandBuffers();
    void recordFrame(std::uint32_t imageIndex,
                     const Camera& camera,
                     std::span<const ModelDraw> draws,
                     float exposure,
                     const GridDraw* grid);
    void updateGrid(const GridDraw* grid);
    void recordGrid(VkCommandBuffer command, const GridDraw& grid, bool shadow = false);
    [[nodiscard]] bool recreateSwapchain();
    [[nodiscard]] VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    [[nodiscard]] VkExtent2D chooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    [[nodiscard]] bool hasDrawableExtent() const;
    void destroySwapchainResources() noexcept;
    void destroy() noexcept;

    SDL_Window* window_ = nullptr;
    bool vsync_ = true, capturePending_ = false, captureSupported_ = false;
    std::optional<Buffer> captureBuffer_;
    FrameCapture capture_;
    VulkanContext context_;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    struct NodeDraw {
        std::vector<std::size_t> meshes;
        std::size_t node = 0;
    };

    struct GpuModel {
        std::vector<GpuMesh> meshes;
        std::vector<NodeDraw> nodes; // Static selected-scene draw cache, not baked geometry.
        std::vector<std::size_t> meshMaterials;
        std::vector<glm::vec3> meshCenters;
        std::vector<float> meshRadii;
        std::unique_ptr<GpuMaterials> materials;
        std::vector<VkDescriptorSet> deformationSets;
        assets::ModelPose restPose;
    };

    struct PrimitiveDraw {
        const GpuModel* model;
        std::size_t mesh;
        DrawConstants constants;
        const MaterialBinding* material;
        bool mirrored;
        float depth;
        std::size_t order;
        std::uint32_t deformation;
        bool castsShadow = true;
    };

    [[nodiscard]] std::span<PrimitiveDraw> preparePrimitiveDraws(const Camera& camera,
                                                                 std::span<const ModelDraw> draws);
    static void sortPrimitiveDraws(std::span<PrimitiveDraw> primitives);
    void recordPrimitiveDraws(VkCommandBuffer command, std::span<const PrimitiveDraw> primitives, bool shadow = false);

    std::vector<std::unique_ptr<GpuModel>> models_;
    std::vector<PrimitiveDraw> primitiveDraws_;
    std::optional<Deformation> deformation_;
    GridInstances gridInstances_;
    const Grid* cachedGrid_ = nullptr;
    std::uint64_t gridRevision_ = 0;
    std::optional<Buffer> gridBuffer_;

    VkDescriptorSetLayout cameraLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet cameraSet_ = VK_NULL_HANDLE;
    std::optional<Buffer> cameraBuffer_;
    std::optional<Buffer> lightBuffer_;
    std::optional<Texture> irradiance_, specularEnvironment_, brdfLut_;
    VkSampler environmentSampler_ = VK_NULL_HANDLE;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;
    std::optional<Buffer> shadowBuffer_;
    std::optional<DepthTarget> shadowDepth_;
    std::optional<MeshPipeline> shadowPipeline_, shadowGridPipeline_;
    VkFormat shadowFormat_ = VK_FORMAT_UNDEFINED;
    std::uint32_t maxShadowResolution_ = 0;
    bool shadowResourcesDirty_ = true;
    bool shadowInitialized_ = false;
    bool shadowReadback_ = false;
    ShadowSettings shadowSettings_;
    RenderSettings renderSettings_;
    bool filteringDirty_ = false;
    ShadowDebugInfo shadowDebug_;
    VkDescriptorSetLayout toneMapLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet toneMapSet_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialLayout_ = VK_NULL_HANDLE;
    std::optional<Texture> whiteTexture_;
    std::optional<MeshPipeline> pipeline_;
    std::optional<MeshPipeline> gridPipeline_;
    std::optional<ToneMapPipeline> toneMapPipeline_;
    std::optional<Texture> hdrColor_;
    std::optional<Texture> msaaColor_;
    std::optional<Texture> compositionColor_;
    VkFormat supportedPresentationFormat_ = VK_FORMAT_UNDEFINED;
    VkSampleCountFlags supportedSamples_ = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits pipelineSamples_ = VK_SAMPLE_COUNT_1_BIT;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;

    // One depth target and camera buffer suffice while there is one frame in flight.
    std::optional<DepthTarget> depth_;
    std::vector<VkCommandBuffer> commandBuffers_;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    std::vector<VkSemaphore> renderFinished_;
    VkFence inFlight_ = VK_NULL_HANDLE;
    bool framebufferResized_ = false;
    RenderOverlay* overlay_ = nullptr;
    std::uint32_t minimumImages_ = 2;
    bool performanceEnabled_ = false, timestampPending_ = false;
    bool timestampsChecked_ = false;
    FramePerformance performance_;
    VkQueryPool timestamps_ = VK_NULL_HANDLE;
    std::uint32_t timestampBits_ = 0;
    float timestampPeriod_ = 0;
};

} // namespace grimlock::renderer
