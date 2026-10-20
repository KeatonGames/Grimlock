#pragma once

#include "platform/runtime_paths.hpp"
#include "renderer/vulkan_helpers.hpp"

#include <fstream>
#include <vector>

namespace grimlock::renderer {

// Short-lived modules shared by the mesh and tone-map pipeline builders.
class ShaderModule final {
public:
    ShaderModule(VkDevice device, const std::filesystem::path& relativePath) : device_(device) {
        const auto path = platform::runtimePath(relativePath);
        std::ifstream file(path, std::ios::binary | std::ios::ate);

        if (!file) {
            throw std::runtime_error("Cannot open shader: " + path.string() + ". Build GrimlockShaders.");
        }

        const auto bytes = static_cast<std::streamoff>(file.tellg());

        if (bytes < 20 || bytes % 4 != 0) {
            throw std::runtime_error("Invalid SPIR-V size: " + path.string());
        }

        std::vector<std::uint32_t> code(static_cast<std::size_t>(bytes) / 4);
        file.seekg(0);

        if (!file.read(reinterpret_cast<char*>(code.data()), bytes) || code.front() != 0x07230203U) {
            throw std::runtime_error("Invalid SPIR-V: " + path.string());
        }

        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = static_cast<std::size_t>(bytes);
        info.pCode = code.data();
        checkVk(vkCreateShaderModule(device_, &info, nullptr, &module_), "vkCreateShaderModule");
    }

    ~ShaderModule() {
        vkDestroyShaderModule(device_, module_, nullptr);
    }

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    [[nodiscard]] VkShaderModule handle() const noexcept {
        return module_;
    }

private:
    VkDevice device_;
    VkShaderModule module_ = VK_NULL_HANDLE;
};

} // namespace grimlock::renderer
