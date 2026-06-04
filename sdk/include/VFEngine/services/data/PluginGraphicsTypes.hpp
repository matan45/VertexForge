#pragma once
#include <vulkan/vulkan.hpp>
#include <vector>

namespace services::plugingfx {

    struct PluginPipelineConfig {
        // Vertex input
        std::vector<vk::VertexInputBindingDescription> vertexBindings;
        std::vector<vk::VertexInputAttributeDescription> vertexAttributes;
        vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;

        // Rasterization
        vk::CullModeFlags cullMode = vk::CullModeFlagBits::eNone;
        vk::PolygonMode polygonMode = vk::PolygonMode::eFill;

        // Depth
        bool depthTestEnable = true;
        bool depthWriteEnable = false;
        vk::CompareOp depthCompareOp = vk::CompareOp::eLess;

        // Blend
        bool blendEnable = false;
        vk::BlendFactor srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        vk::BlendFactor dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        vk::BlendFactor srcAlphaBlendFactor = vk::BlendFactor::eOne;
        vk::BlendFactor dstAlphaBlendFactor = vk::BlendFactor::eZero;

        // Descriptor sets
        std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;

        // Push constants
        uint32_t pushConstantSize = 0;
        vk::ShaderStageFlags pushConstantStages = vk::ShaderStageFlagBits::eVertex;
    };

    struct PluginPipelineResult {
        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;
        bool isValid() const { return static_cast<bool>(pipeline); }
    };

}
