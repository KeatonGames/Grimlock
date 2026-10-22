#pragma once

#include "renderer/buffer.hpp"

#include <glm/glm.hpp>
#include <span>
#include <vector>

namespace grimlock::renderer {

struct DeformationFrameSize {
    std::size_t draws = 0, joints = 0, weights = 0;
};

// One fence-protected frame arena; immutable mesh sets share its three buffers.
class Deformation final {
public:
    explicit Deformation(const VulkanContext& context);
    ~Deformation();
    Deformation(const Deformation&) = delete;
    Deformation& operator=(const Deformation&) = delete;

    [[nodiscard]] VkDescriptorSetLayout layout() const noexcept {
        return layout_;
    }

    [[nodiscard]] VkDescriptorSet meshSet(VkBuffer morphs, VkDeviceSize size);
    void releaseMeshSets(std::span<const VkDescriptorSet> sets) noexcept;
    void begin();

    struct NodeData {
        std::uint32_t jointOffset = 0, weightOffset = 0;
        std::uint32_t jointCount = 0, weightCount = 0;
    };

    [[nodiscard]] NodeData appendNode(std::span<const glm::mat4> joints, std::span<const float> weights);
    [[nodiscard]] std::uint32_t append(NodeData node, std::uint32_t morphVertices, std::uint32_t morphTargets);
    void validateFrameSize(DeformationFrameSize size) const;

    [[nodiscard]] DeformationFrameSize size() const noexcept {
        return {records_.size(), joints_.size(), weights_.size()};
    }

    // Caller must have waited for the frame fence before replacing buffers/descriptors.
    void upload();

private:
    static constexpr std::uint32_t initialDraws = 4096, initialJoints = 65536, initialWeights = 65536;
    static constexpr std::uint32_t maxMeshSets = 4096; // Unique GPU primitives, independent of instance count.
    void growBuffers();
    void refreshArenaBindings();

    struct Record {
        glm::uvec4 offsets{};
        glm::uvec4 counts{};
    };

    static_assert(sizeof(Record) == 32);
    const VulkanContext& context_;
    VkDevice device_;
    VkDeviceSize storageLimit_ = 0;
    DeformationFrameSize limits_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    Buffer recordsBuffer_, jointsBuffer_, weightsBuffer_;
    std::vector<Record> records_;
    std::vector<glm::mat4> joints_;
    std::vector<float> weights_;
    std::vector<VkDescriptorSet> meshSets_;
};

} // namespace grimlock::renderer
