#include "renderer/buffer.hpp"

#include "core/startup_profile.hpp"
#include "renderer/vulkan_helpers.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace grimlock::renderer {

Buffer::Buffer(const VulkanContext& context, const VkDeviceSize size,
               const VkBufferUsageFlags usage, const VkMemoryPropertyFlags properties)
    : device_(context.device()), size_(size) {
    diagnostics::StartupSpan profile{"GPU.buffer.create"};
    if (size == 0) {
        throw std::invalid_argument("Cannot allocate an empty GPU buffer.");
    }
    if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 &&
        (properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        throw std::invalid_argument("Mapped Buffer requires coherent memory.");
    }
    try {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        checkVk(vkCreateBuffer(device_, &info, nullptr, &buffer_), "vkCreateBuffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, buffer_, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex =
            findMemoryType(context.physicalDevice(), requirements.memoryTypeBits, properties);
        checkVk(vkAllocateMemory(device_, &allocation, nullptr, &memory_),
                "vkAllocateMemory(buffer)");
        checkVk(vkBindBufferMemory(device_, buffer_, memory_, 0), "vkBindBufferMemory");
        if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
            checkVk(vkMapMemory(device_, memory_, 0, size_, 0, &mapped_), "vkMapMemory");
        }
    } catch (...) {
        destroy();
        throw;
    }
}

Buffer::~Buffer() {
    destroy();
}

Buffer::Buffer(Buffer&& other) noexcept {
    *this = std::move(other);
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
        size_ = std::exchange(other.size_, 0);
        mapped_ = std::exchange(other.mapped_, nullptr);
    }
    return *this;
}

void Buffer::write(const void* data, const VkDeviceSize bytes) {
    if (mapped_ == nullptr || data == nullptr || bytes > size_) {
        throw std::invalid_argument("Invalid write to a mapped GPU buffer.");
    }
    std::memcpy(mapped_, data, static_cast<std::size_t>(bytes));
}

void Buffer::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        if (mapped_ != nullptr) {
            vkUnmapMemory(device_, memory_);
        }
        vkDestroyBuffer(device_, buffer_, nullptr);
        vkFreeMemory(device_, memory_, nullptr);
    }
    buffer_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    mapped_ = nullptr;
}

} // namespace grimlock::renderer
