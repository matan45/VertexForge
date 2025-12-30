#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>

namespace core
{
	struct WireframePipelineConfig
	{
		vk::Device device;
		vk::RenderPass renderPass;
		vk::Extent2D extent;
		uint32_t pushConstantSize;
		const std::vector<vk::PipelineShaderStageCreateInfo>& shaderStages;
		bool enableBlending = false;
	};

	struct WireframePipelineResult
	{
		vk::Pipeline pipeline;
		vk::PipelineLayout pipelineLayout;
	};

	struct GraphicsPipelineConfig
	{
		vk::Device device;
		vk::RenderPass renderPass;
		vk::Extent2D extent;
		const std::vector<vk::PipelineShaderStageCreateInfo>& shaderStages;

		// Vertex input
		std::vector<vk::VertexInputBindingDescription> vertexBindings;
		std::vector<vk::VertexInputAttributeDescription> vertexAttributes;

		// Topology
		vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;

		// Pipeline layout - provide existing OR set descriptorSetLayouts to create new
		vk::PipelineLayout existingPipelineLayout = nullptr;
		std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;

		// Push constants (only used if existingPipelineLayout is null)
		uint32_t pushConstantSize = 0;
		vk::ShaderStageFlags pushConstantStages = vk::ShaderStageFlagBits::eVertex;

		// Rasterization
		vk::CullModeFlags cullMode = vk::CullModeFlagBits::eNone;
		vk::PolygonMode polygonMode = vk::PolygonMode::eFill;

		// Depth
		bool depthTestEnable = true;
		bool depthWriteEnable = false;
		vk::CompareOp depthCompareOp = vk::CompareOp::eLess;

		// Blending
		bool blendEnable = false;
		vk::BlendFactor srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		vk::BlendFactor dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		vk::BlendFactor srcAlphaBlendFactor = vk::BlendFactor::eOne;
		vk::BlendFactor dstAlphaBlendFactor = vk::BlendFactor::eZero;
	};

	struct GraphicsPipelineResult
	{
		vk::Pipeline pipeline;
		vk::PipelineLayout pipelineLayout;
	};

	class PipelineUtilities
	{
	private:
		PipelineUtilities() = delete;
		~PipelineUtilities() = delete;

	public:
		static WireframePipelineResult createWireframePipeline(const WireframePipelineConfig& config);
		static GraphicsPipelineResult createGraphicsPipeline(const GraphicsPipelineConfig& config);
	};
}
