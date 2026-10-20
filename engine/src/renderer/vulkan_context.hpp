#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

struct SDL_Window;

namespace grimlock::renderer {

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// Owns the Vulkan instance/surface/device. All GPU resources must die before it.
class VulkanContext final {
public:
    explicit VulkanContext(SDL_Window* window);
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }

    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }

    [[nodiscard]] VkDevice device() const noexcept { return device_; }

    [[nodiscard]] VkSurfaceKHR surface() const noexcept { return surface_; }

    [[nodiscard]] VkQueue graphicsQueue() const noexcept { return graphicsQueue_; }

    [[nodiscard]] VkQueue presentQueue() const noexcept { return presentQueue_; }

    [[nodiscard]] std::uint32_t graphicsFamily() const noexcept { return *families_.graphics; }

    [[nodiscard]] std::uint32_t presentFamily() const noexcept { return *families_.present; }

    [[nodiscard]] SwapchainSupport swapchainSupport() const;

private:
    struct QueueFamilies {
        std::optional<std::uint32_t> graphics;
        std::optional<std::uint32_t> present;

        [[nodiscard]] bool complete() const noexcept { return graphics && present; }
    };

    void createInstance();
    void selectPhysicalDevice();
    void createDevice();
    void destroy() noexcept;
    [[nodiscard]] QueueFamilies findQueueFamilies(VkPhysicalDevice device) const;
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const;
    [[nodiscard]] SwapchainSupport querySwapchainSupport(VkPhysicalDevice device) const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    QueueFamilies families_;
};

} // namespace grimlock::renderer
