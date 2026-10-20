#pragma once

#include "grimlock/assets/image_data.hpp"
#include "renderer/vulkan_context.hpp"

#include <span>

namespace grimlock::renderer {

enum class TextureColorSpace { srgb, linear };

class Texture final {
public:
    Texture(const VulkanContext& context, VkCommandPool uploadPool, const assets::ImageData& data,
            TextureColorSpace colorSpace = TextureColorSpace::srgb);
    // Preprocessed linear float mip/face images: mip-major, six faces for a cube.
    Texture(const VulkanContext& context, VkCommandPool uploadPool,
            std::span<const assets::FloatImageData> levels, bool cube);
    // Render target, owned by the same image/memory/view RAII as sampled textures.
    Texture(const VulkanContext& context, VkExtent2D extent, VkFormat format,
            VkImageUsageFlags usage);
    ~Texture();
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    [[nodiscard]] VkImageView view() const noexcept { return view_; }

    [[nodiscard]] VkImage image() const noexcept { return image_; }

    [[nodiscard]] std::uint32_t mipLevels() const noexcept { return mipLevels_; }

    [[nodiscard]] bool linearFiltering() const noexcept { return linearFiltering_; }

private:
    void create(const VulkanContext& context, std::uint32_t width, std::uint32_t height,
                std::uint32_t levels, std::uint32_t layers, VkFormat format,
                VkImageUsageFlags usage);
    void destroy() noexcept;
    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    std::uint32_t mipLevels_ = 1;
    bool linearFiltering_ = false;
};

} // namespace grimlock::renderer
