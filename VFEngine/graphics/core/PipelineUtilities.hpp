#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>

namespace core
{
	struct WireframePipelineConfig
	{
		vk::Device device;
		vk::RenderPass renderPass; // nullptr for dynamic rendering
		vk::Extent2D extent;

		// Dynamic rendering formats (used when renderPass is nullptr)
		std::vector<vk::Format> colorAttachmentFormats;
		vk::Format depthAttachmentFormat = vk::Format::eUndefined;
		vk::Format stencilAttachmentFormat = vk::Format::eUndefined;
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
		vk::RenderPass renderPass; // nullptr for dynamic rendering
		vk::Extent2D extent;

		// Dynamic rendering formats (used when renderPass is nullptr)
		std::vector<vk::Format> colorAttachmentFormats;
		vk::Format depthAttachmentFormat = vk::Format::eUndefined;
		vk::Format stencilAttachmentFormat = vk::Format::eUndefined;
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

		bool stencilTestEnable = false;
		vk::StencilOpState stencilFront{};
		vk::StencilOpState stencilBack{};
		vk::ColorComponentFlags colorWriteMask = vk::ColorComponentFlagBits::eR |
		                                         vk::ColorComponentFlagBits::eG |
		                                         vk::ColorComponentFlagBits::eB |
		                                         vk::ColorComponentFlagBits::eA;

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
		vk::RenderPass renderPass; // nullptr for dynamic rendering
		vk::Extent2D extent;

		// Dynamic rendering formats (used when renderPass is nullptr)
		std::vector<vk::Format> colorAttachmentFormats;
		vk::Format depthAttachmentFormat = vk::Format::eUndefined;
		vk::Format stencilAttachmentFormat = vk::Format::eUndefined;
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

		// When non-empty, overrides the single blend attachment above with per-attachment states
		std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;

		std::vector<vk::DynamicState> dynamicStates;
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

		// Threading contract: setGlobalPipelineCache() must be called once during
		// VulkanContext::init() on the main thread BEFORE any pipeline creation.
		// After initialization the value is read-only, so concurrent reads are safe.
		// Do NOT call setGlobalPipelineCache() after init or from worker threads.
		static inline vk::PipelineCache globalPipelineCache = nullptr;

	public:
		static void setGlobalPipelineCache(vk::PipelineCache cache);

		static WireframePipelineResult createWireframePipeline(const WireframePipelineConfig& config);
		static GraphicsPipelineResult createGraphicsPipeline(const GraphicsPipelineConfig& config);
		static MeshShaderPipelineResult createMeshShaderPipeline(const MeshShaderPipelineConfig& config);

		static vk::Pipeline createComputePipeline(vk::Device device, const vk::ComputePipelineCreateInfo& info);

		/// Create a descriptor set layout with eUpdateAfterBind on all bindings.
		static vk::DescriptorSetLayout createUpdateAfterBindLayout(
			vk::Device device,
			const vk::DescriptorSetLayoutBinding* bindings,
			uint32_t bindingCount);

		/// Create a descriptor pool with eUpdateAfterBind flag.
		static vk::DescriptorPool createUpdateAfterBindPool(
			vk::Device device,
			uint32_t maxSets,
			const vk::DescriptorPoolSize* poolSizes,
			uint32_t poolSizeCount,
			vk::DescriptorPoolCreateFlags extraFlags = {});
	};
}
