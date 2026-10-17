#include "core/startup_profile.hpp"
#include "renderer/vulkan_context.hpp"
#include "renderer/vulkan_helpers.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace grimlock::renderer {

VulkanContext::VulkanContext(SDL_Window* window) {
    diagnostics::StartupSpan profile{"vulkan.instance_device_surface"};
    if (window == nullptr) {
        throw std::invalid_argument("VulkanContext requires a valid SDL window.");
    }
    try {
        createInstance();
        if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_)) {
            throw std::runtime_error(std::string{"SDL_Vulkan_CreateSurface failed: "} + SDL_GetError());
        }
        selectPhysicalDevice();
        createDevice();
    } catch (...) {
        destroy();
        throw;
    }
}

VulkanContext::~VulkanContext() { destroy(); }

void VulkanContext::createInstance() {
    const auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    std::uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (enumerateVersion != nullptr) {
        checkVk(enumerateVersion(&loaderVersion), "vkEnumerateInstanceVersion");
    }
    if (loaderVersion < VK_API_VERSION_1_3) {
        throw std::runtime_error("Grimlock requires a Vulkan 1.3+ loader for Dynamic Rendering and Synchronization2.");
    }
    Uint32 extensionCount = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (extensions == nullptr) {
        throw std::runtime_error(std::string{"SDL_Vulkan_GetInstanceExtensions failed: "} + SDL_GetError());
    }
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Grimlock";
    application.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    application.pEngineName = "Grimlock";
    application.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    application.apiVersion = std::min(loaderVersion, VK_MAKE_API_VERSION(0, 1, 4, 0));
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &application;
    info.enabledExtensionCount = extensionCount;
    info.ppEnabledExtensionNames = extensions;
    checkVk(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance");
}

VulkanContext::QueueFamilies VulkanContext::findQueueFamilies(const VkPhysicalDevice device) const {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
    QueueFamilies result;
    for (std::uint32_t index = 0; index < count; ++index) {
        if ((properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            result.graphics = index;
        }
        VkBool32 present = VK_FALSE;
        checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface_, &present),
            "vkGetPhysicalDeviceSurfaceSupportKHR");
        if (present == VK_TRUE) {
            result.present = index;
        }
        if (result.complete()) { break; }
    }
    return result;
}

SwapchainSupport VulkanContext::querySwapchainSupport(const VkPhysicalDevice device) const {
    SwapchainSupport support;
    checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &support.capabilities),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    std::uint32_t count = 0;
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &count, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
    support.formats.resize(count);
    if (count != 0) {
        checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &count, support.formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    }
    checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &count, nullptr),
        "vkGetPhysicalDeviceSurfacePresentModesKHR");
    support.presentModes.resize(count);
    if (count != 0) {
        checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &count, support.presentModes.data()),
            "vkGetPhysicalDeviceSurfacePresentModesKHR");
    }
    return support;
}

SwapchainSupport VulkanContext::swapchainSupport() const { return querySwapchainSupport(physicalDevice_); }

bool VulkanContext::isDeviceSuitable(const VkPhysicalDevice device) const {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_3 || !findQueueFamilies(device).complete()) { return false; }
    VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &features;
    vkGetPhysicalDeviceFeatures2(device, &features2);
    if (!features.dynamicRendering || !features.synchronization2) { return false; }

    std::uint32_t count = 0;
    checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr), "vkEnumerateDeviceExtensionProperties");
    std::vector<VkExtensionProperties> extensions(count);
    checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()),
        "vkEnumerateDeviceExtensionProperties");
    if (!std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties& extension) {
        return std::string_view{extension.extensionName} == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    })) { return false; }

    const auto support = querySwapchainSupport(device);
    return !support.formats.empty() && !support.presentModes.empty()
        && (support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0;
}

void VulkanContext::selectPhysicalDevice() {
    std::uint32_t count = 0;
    checkVk(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "vkEnumeratePhysicalDevices");
    if (count == 0) { throw std::runtime_error("No Vulkan-capable GPU is available."); }
    std::vector<VkPhysicalDevice> devices(count);
    checkVk(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "vkEnumeratePhysicalDevices");
    int bestScore = -1;
    for (const auto device : devices) {
        if (!isDeviceSuitable(device)) { continue; }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1 : 0;
        if (score > bestScore) { physicalDevice_ = device; bestScore = score; }
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error("No Vulkan 1.3+ device supports Dynamic Rendering, Synchronization2, graphics, and presentation.");
    }
    families_ = findQueueFamilies(physicalDevice_);
}

void VulkanContext::createDevice() {
    const std::set<std::uint32_t> uniqueFamilies{graphicsFamily(), presentFamily()};
    constexpr float priority = 1.0F;
    std::vector<VkDeviceQueueCreateInfo> queues;
    for (const auto family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        queues.push_back(queue);
    }
    VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.dynamicRendering = VK_TRUE;
    features.synchronization2 = VK_TRUE;
    constexpr const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &features;
    info.queueCreateInfoCount = static_cast<std::uint32_t>(queues.size());
    info.pQueueCreateInfos = queues.data();
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;
    checkVk(vkCreateDevice(physicalDevice_, &info, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, graphicsFamily(), 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentFamily(), 0, &presentQueue_);
}

void VulkanContext::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_ != VK_NULL_HANDLE) { vkDestroySurfaceKHR(instance_, surface_, nullptr); surface_ = VK_NULL_HANDLE; }
    if (instance_ != VK_NULL_HANDLE) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
}

} // namespace grimlock::renderer
