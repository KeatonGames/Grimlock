#pragma once

#include "grimlock/assets/model_data.hpp"
#include "renderer/buffer.hpp"

#include <cstdint>

namespace grimlock::renderer {

class GpuMesh final {
public:
    GpuMesh(const VulkanContext& context, VkCommandPool uploadPool, const assets::MeshData& data);
    GpuMesh(GpuMesh&&) noexcept = default;
    GpuMesh& operator=(GpuMesh&&) noexcept = default;
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;

    void draw(VkCommandBuffer command, std::uint32_t firstInstance = 0, std::uint32_t instanceCount = 1) const;

    [[nodiscard]] VkBuffer morphBuffer() const noexcept {
        return morphs_.handle();
    }

    [[nodiscard]] VkDeviceSize morphSize() const noexcept {
        return morphs_.size();
    }

    std::uint32_t morphVertexCount = 0, morphTargetCount = 0;

private:
    Buffer vertices_;
    Buffer indices_;
    Buffer morphs_;
    std::uint32_t indexCount_ = 0;
};

} // namespace grimlock::renderer
