#include "core/startup_profile.hpp"
#include "renderer/gpu_mesh.hpp"
#include "renderer/vulkan_helpers.hpp"

#include <limits>
#include <stdexcept>

namespace grimlock::renderer {

GpuMesh::GpuMesh(const VulkanContext& context, const VkCommandPool uploadPool, const assets::MeshData& data)
    : vertices_(context, data.vertices.size() * sizeof(assets::Vertex),
          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    , indices_(context, data.indices.size() * sizeof(std::uint32_t),
          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    , morphs_(context, std::max(std::size_t{1},data.morphDeltas.size()) * sizeof(assets::MeshData::MorphDelta),
          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
    diagnostics::StartupSpan profile{"GPU.mesh.upload"};
    morphVertexCount=data.morphVertexCount; morphTargetCount=data.morphTargetCount;
    VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(context.physicalDevice(),&properties);
    if (morphs_.size() > properties.limits.maxStorageBufferRange) { throw std::runtime_error("Morph data exceeds maxStorageBufferRange."); }
    if (data.indices.size() > std::numeric_limits<std::uint32_t>::max() || data.indices.size() % 3 != 0) {
        throw std::invalid_argument("GPU mesh needs a 32-bit triangle index list.");
    }
    for (const auto index : data.indices) {
        if (index >= data.vertices.size()) { throw std::invalid_argument("GPU mesh index is out of bounds."); }
    }
    indexCount_ = static_cast<std::uint32_t>(data.indices.size());
    constexpr auto hostMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    Buffer vertexStaging{context, vertices_.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, hostMemory};
    Buffer indexStaging{context, indices_.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, hostMemory};
    vertexStaging.write(data.vertices.data(), vertices_.size());
    indexStaging.write(data.indices.data(), indices_.size());
    Buffer morphStaging{context,morphs_.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,hostMemory};
    const assets::MeshData::MorphDelta zero{};
    morphStaging.write(data.morphDeltas.empty()?&zero:data.morphDeltas.data(),morphs_.size());

    submitUpload(context, uploadPool, [&](const VkCommandBuffer command) {
        const VkBufferCopy vertexCopy{0, 0, vertices_.size()};
        const VkBufferCopy indexCopy{0, 0, indices_.size()};
        vkCmdCopyBuffer(command, vertexStaging.handle(), vertices_.handle(), 1, &vertexCopy);
        vkCmdCopyBuffer(command, indexStaging.handle(), indices_.handle(), 1, &indexCopy);
        const VkBufferCopy morphCopy{0,0,morphs_.size()};
        vkCmdCopyBuffer(command,morphStaging.handle(),morphs_.handle(),1,&morphCopy);
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command, &dependency);
    });
}

void GpuMesh::draw(const VkCommandBuffer command, const std::uint32_t firstInstance) const {
    const VkBuffer buffer = vertices_.handle();
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &buffer, &offset);
    vkCmdBindIndexBuffer(command, indices_.handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(command, indexCount_, 1, 0, 0, firstInstance);
}

} // namespace grimlock::renderer
