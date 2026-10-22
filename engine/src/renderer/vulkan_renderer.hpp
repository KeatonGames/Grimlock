#pragma once

#include "assets/environment.hpp"
#include "grimlock/assets/animation.hpp"
#include "grimlock/camera.hpp"
#include "grimlock/render_overlay.hpp"
#include "renderer/buffer.hpp"
#include "renderer/deformation.hpp"
#include "renderer/depth_target.hpp"
#include "renderer/gpu_materials.hpp"
#include "renderer/gpu_mesh.hpp"
#include "renderer/grid_instances.hpp"
#include "renderer/lighting.hpp"
#include "renderer/mesh_pipeline.hpp"
#include "renderer/tone_map_pipeline.hpp"
#include "renderer/vulkan_context.hpp"

#include <cstddef>
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

private:
    void createCommandPool();
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
    void recordGrid(VkCommandBuffer command, const GridDraw& grid);
    [[nodiscard]] bool recreateSwapchain();
    [[nodiscard]] VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    [[nodiscard]] VkExtent2D chooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    [[nodiscard]] bool hasDrawableExtent() const;
    void destroySwapchainResources() noexcept;
    void destroy() noexcept;

    SDL_Window* window_ = nullptr;
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
    };

    [[nodiscard]] std::vector<PrimitiveDraw> preparePrimitiveDraws(const Camera& camera,
                                                                   std::span<const ModelDraw> draws);
    static void sortPrimitiveDraws(std::span<PrimitiveDraw> primitives);
    void recordPrimitiveDraws(VkCommandBuffer command, std::span<const PrimitiveDraw> primitives);

    std::vector<std::unique_ptr<GpuModel>> models_;
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
    VkDescriptorSetLayout toneMapLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet toneMapSet_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialLayout_ = VK_NULL_HANDLE;
    std::optional<Texture> whiteTexture_;
    std::optional<MeshPipeline> pipeline_;
    std::optional<MeshPipeline> gridPipeline_;
    std::optional<ToneMapPipeline> toneMapPipeline_;
    std::optional<Texture> hdrColor_;

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
};

} // namespace grimlock::renderer
