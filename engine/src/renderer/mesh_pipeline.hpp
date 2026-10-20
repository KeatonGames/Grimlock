#pragma once

#include <array>
#include <cstddef>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace grimlock::renderer {

struct alignas(16) CameraUniforms {
    glm::mat4 view{1.0F};
    glm::mat4 projection{1.0F};
    glm::vec4 position{};
};

// Fits Vulkan's guaranteed 128-byte push-constant limit, including a normal matrix.
struct alignas(16) DrawConstants {
    glm::mat4 model{1.0F};
    glm::vec4 normalColumn0{};
    glm::vec4 normalColumn1{}; // W is a transient per-object selection flag.
    glm::vec4 normalColumn2{};
    glm::vec4 baseColor{1.0F};
};

static_assert(sizeof(CameraUniforms) == 144);
static_assert(sizeof(DrawConstants) == 128);
static_assert(offsetof(CameraUniforms, projection) == 64);
static_assert(offsetof(DrawConstants, normalColumn0) == 64);
static_assert(offsetof(DrawConstants, baseColor) == 112);

class MeshPipeline final {
public:
    MeshPipeline(VkDevice device,
                 VkFormat colorFormat,
                 VkFormat depthFormat,
                 VkDescriptorSetLayout cameraLayout,
                 VkDescriptorSetLayout materialLayout,
                 VkDescriptorSetLayout deformationLayout);
    ~MeshPipeline();

    MeshPipeline(const MeshPipeline&) = delete;
    MeshPipeline& operator=(const MeshPipeline&) = delete;

    [[nodiscard]] VkPipeline handle(bool blend, bool doubleSided, bool mirrored) const noexcept;

    [[nodiscard]] VkPipelineLayout layout() const noexcept {
        return layout_;
    }

private:
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;

    // Fixed state variants, shared by all compatible materials and model instances.
    std::array<VkPipeline, 8> pipelines_{};
};

} // namespace grimlock::renderer
