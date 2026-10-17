#pragma once
#include <vulkan/vulkan.h>

namespace grimlock::renderer {
class ToneMapPipeline final {
public:
    ToneMapPipeline(VkDevice device, VkFormat colorFormat, VkDescriptorSetLayout layout);
    ~ToneMapPipeline();
    ToneMapPipeline(const ToneMapPipeline&) = delete;
    ToneMapPipeline& operator=(const ToneMapPipeline&) = delete;
    [[nodiscard]] VkPipeline handle() const noexcept { return pipeline_; }
    [[nodiscard]] VkPipelineLayout layout() const noexcept { return layout_; }
    [[nodiscard]] VkFormat colorFormat() const noexcept { return colorFormat_; }
private:
    void destroy() noexcept;
    VkDevice device_;
    VkFormat colorFormat_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
}
