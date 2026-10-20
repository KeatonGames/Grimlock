#pragma once
#include "renderer/buffer.hpp"

#include <glm/glm.hpp>
#include <span>
#include <vector>

namespace grimlock::renderer {
// One fence-protected frame arena; immutable mesh sets share its three buffers.
class Deformation final {
public:
    explicit Deformation(const VulkanContext& context);
    ~Deformation();
    Deformation(const Deformation&) = delete;
    Deformation& operator=(const Deformation&) = delete;

    [[nodiscard]] VkDescriptorSetLayout layout() const noexcept { return layout_; }

    [[nodiscard]] VkDescriptorSet meshSet(VkBuffer morphs, VkDeviceSize size);
    void releaseMeshSets(std::span<const VkDescriptorSet> sets) noexcept;
    void begin();
    [[nodiscard]] std::uint32_t append(std::span<const glm::mat4> joints,
                                       std::span<const float> weights, std::uint32_t morphVertices,
                                       std::uint32_t morphTargets);
    void upload();

private:
    static constexpr std::uint32_t maxDraws = 4096, maxJoints = 65536, maxWeights = 65536;

    struct Record {
        glm::uvec4 offsets{};
        glm::uvec4 counts{};
    };

    static_assert(sizeof(Record) == 32);
    VkDevice device_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    Buffer recordsBuffer_, jointsBuffer_, weightsBuffer_;
    std::vector<Record> records_;
    std::vector<glm::mat4> joints_;
    std::vector<float> weights_;
};
} // namespace grimlock::renderer
