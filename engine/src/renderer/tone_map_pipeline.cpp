#include "core/startup_profile.hpp"
#include "renderer/tone_map_pipeline.hpp"
#include "renderer/shader_module.hpp"
#include <array>

namespace grimlock::renderer {
ToneMapPipeline::ToneMapPipeline(const VkDevice device, const VkFormat colorFormat, const VkDescriptorSetLayout layout)
    : device_(device), colorFormat_(colorFormat) {
    try {
    diagnostics::StartupSpan profile{"pipeline.tonemap"};
        const ShaderModule shader{device_, "shaders/tonemap.spv"};
        const VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float)};
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1; layoutInfo.pSetLayouts = &layout;
        layoutInfo.pushConstantRangeCount = 1; layoutInfo.pPushConstantRanges = &push;
        checkVk(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout_), "vkCreatePipelineLayout(tonemap)");
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[0].module = stages[1].module = shader.handle(); stages[0].pName = "vertMain"; stages[1].pName = "fragMain";
        VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1; blend.pAttachments = &attachment;
        constexpr std::array states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(states.size()); dynamic.pDynamicStates = states.data();
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &colorFormat_;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering; info.stageCount = 2; info.pStages = stages.data();
        info.pVertexInputState = &vertex; info.pInputAssemblyState = &assembly; info.pViewportState = &viewport;
        info.pRasterizationState = &raster; info.pMultisampleState = &samples; info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic; info.layout = layout_;
        checkVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_), "vkCreateGraphicsPipelines(tonemap)");
    } catch (...) { destroy(); throw; }
}
ToneMapPipeline::~ToneMapPipeline() { destroy(); }
void ToneMapPipeline::destroy() noexcept {
    vkDestroyPipeline(device_, pipeline_, nullptr); vkDestroyPipelineLayout(device_, layout_, nullptr);
    pipeline_ = VK_NULL_HANDLE; layout_ = VK_NULL_HANDLE;
}
}
