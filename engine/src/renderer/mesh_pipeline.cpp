#include "renderer/mesh_pipeline.hpp"

#include "core/startup_profile.hpp"
#include "grimlock/assets/model_data.hpp"
#include "renderer/grid_instances.hpp"
#include "renderer/shader_module.hpp"
#include "renderer/vulkan_helpers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace grimlock::renderer {

MeshPipeline::MeshPipeline(const VkDevice device,
                           const VkFormat colorFormat,
                           const VkFormat depthFormat,
                           const VkDescriptorSetLayout cameraLayout,
                           const VkDescriptorSetLayout materialLayout,
                           const VkDescriptorSetLayout deformationLayout,
                           const bool gridInstances)
    : device_(device) {
    try {
        diagnostics::StartupSpan profile{"pipeline.mesh"};
        const ShaderModule shader{device_, gridInstances ? "shaders/grid.spv" : "shaders/mesh.spv"};

        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        const VkPushConstantRange pushRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0,
                                            sizeof(DrawConstants)};
        const std::array layouts{cameraLayout, materialLayout, deformationLayout};
        layoutInfo.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
        layoutInfo.pSetLayouts = layouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        checkVk(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout_), "vkCreatePipelineLayout");

        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = shader.handle();
        stages[0].pName = "vertMain";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = shader.handle();
        stages[1].pName = "fragMain";

        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        static_assert(sizeof(assets::Vertex) == 124);
        const std::array<VkVertexInputBindingDescription, 2> bindings{{
            {0, sizeof(assets::Vertex), VK_VERTEX_INPUT_RATE_VERTEX},
            {1, sizeof(GridCubeInstance), VK_VERTEX_INPUT_RATE_INSTANCE},
        }};
        const std::array<VkVertexInputAttributeDescription, 12> attributes{{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(assets::Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(assets::Vertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(assets::Vertex, uv)},
            {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(assets::Vertex, uv1)},
            {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(assets::Vertex, tangent)},
            {5, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(assets::Vertex, joints0)},
            {6, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(assets::Vertex, joints1)},
            {7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(assets::Vertex, weights0)},
            {8, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(assets::Vertex, weights1)},
            {9, 0, VK_FORMAT_R32_UINT, offsetof(assets::Vertex, morphIndex)},
            {10, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GridCubeInstance, position)},
            {11, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GridCubeInstance, scale)},
        }};
        vertexInput.vertexBindingDescriptionCount = gridInstances ? 2 : 1;
        vertexInput.pVertexBindingDescriptions = bindings.data();
        vertexInput.vertexAttributeDescriptionCount = gridInstances ? 12 : 10;
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterization{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0F;

        VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState colorAttachment{};
        colorAttachment.blendEnable = VK_FALSE;
        colorAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blending.attachmentCount = 1;
        blending.pAttachments = &colorAttachment;

        constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamic.pDynamicStates = dynamicStates.data();

        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &colorFormat;
        rendering.depthAttachmentFormat = depthFormat;

        VkGraphicsPipelineCreateInfo createInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        createInfo.pNext = &rendering;
        createInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        createInfo.pStages = stages.data();
        createInfo.pVertexInputState = &vertexInput;
        createInfo.pInputAssemblyState = &assembly;
        createInfo.pViewportState = &viewport;
        createInfo.pRasterizationState = &rasterization;
        createInfo.pMultisampleState = &multisampling;
        createInfo.pDepthStencilState = &depthStencil;
        createInfo.pColorBlendState = &blending;
        createInfo.pDynamicState = &dynamic;
        createInfo.layout = layout_;

        const auto variantCount = gridInstances ? std::size_t{1} : pipelines_.size();

        for (std::size_t variant = 0; variant < variantCount; ++variant) {
            const bool blend = (variant & 1U) != 0, doubleSided = (variant & 2U) != 0, mirrored = (variant & 4U) != 0;

            rasterization.cullMode = doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
            rasterization.frontFace = mirrored ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
            depthStencil.depthWriteEnable = blend ? VK_FALSE : VK_TRUE;

            colorAttachment.blendEnable = blend ? VK_TRUE : VK_FALSE;
            colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

            checkVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipelines_[variant]),
                    "vkCreateGraphicsPipelines(material)");
        }
    } catch (...) {
        destroy();
        throw;
    }
}

MeshPipeline::~MeshPipeline() {
    destroy();
}

VkPipeline MeshPipeline::handle(const bool blend, const bool doubleSided, const bool mirrored) const noexcept {
    return pipelines_[(blend ? 1U : 0U) | (doubleSided ? 2U : 0U) | (mirrored ? 4U : 0U)];
}

void MeshPipeline::destroy() noexcept {
    for (const auto pipeline : pipelines_) {
        vkDestroyPipeline(device_, pipeline, nullptr);
    }

    vkDestroyPipelineLayout(device_, layout_, nullptr);
    pipelines_.fill(VK_NULL_HANDLE);
    layout_ = VK_NULL_HANDLE;
}

} // namespace grimlock::renderer
