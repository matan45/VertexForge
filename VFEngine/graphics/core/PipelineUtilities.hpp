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
		std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
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
		std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
		
		std::vector<vk::VertexInputBindingDescription> vertexBindings;
		std::vector<vk::VertexInputAttributeDescription> vertexAttributes;
		
		vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
		
		vk::PipelineLayout existingPipelineLayout = nullptr;
		std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
		
		uint32_t pushConstantSize = 0;
		vk::ShaderStageFlags pushConstantStages = vk::ShaderStageFlagBits::eVertex;
		
		vk::CullModeFlags cullMode = vk::CullModeFlagBits::eNone;
		vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
		
		bool depthTestEnable = true;
		bool depthWriteEnable = false;
		vk::CompareOp depthCompareOp = vk::CompareOp::eLess;
		
		bool depthBiasEnable = false;
		float depthBiasConstantFactor = 0.0f;
		float depthBiasSlopeFactor = 0.0f;

		bool blendEnable = false;
		vk::BlendFactor srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		vk::BlendFactor dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		vk::BlendFactor srcAlphaBlendFactor = vk::BlendFactor::eOne;
		vk::BlendFactor dstAlphaBlendFactor = vk::BlendFactor::eZero;

		std::vector<vk::DynamicState> dynamicStates;
	};

	struct GraphicsPipelineResult
	{
		vk::Pipeline pipeline;
		vk::PipelineLayout pipelineLayout;
	};

	struct MeshShaderPipelineConfig
	{
		vk::Device device;
		vk::RenderPass renderPass;
		vk::Extent2D extent;
		std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
		
		vk::PipelineLayout existingPipelineLayout = nullptr;
		std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
		
		uint32_t pushConstantSize = 0;
		vk::ShaderStageFlags pushConstantStages = vk::ShaderStageFlagBits::eMeshEXT;
		
		vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
		vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
		
		bool depthTestEnable = true;
		bool depthWriteEnable = true;
		vk::CompareOp depthCompareOp = vk::CompareOp::eLess;
		
		bool blendEnable = false;
		vk::BlendFactor srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		vk::BlendFactor dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		vk::BlendFactor srcAlphaBlendFactor = vk::BlendFactor::eOne;
		vk::BlendFactor dstAlphaBlendFactor = vk::BlendFactor::eZero;
	};

	struct MeshShaderPipelineResult
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
		static MeshShaderPipelineResult createMeshShaderPipeline(const MeshShaderPipelineConfig& config);
	};
}
