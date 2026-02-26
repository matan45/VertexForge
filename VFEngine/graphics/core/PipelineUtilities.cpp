#include "PipelineUtilities.hpp"
#include <glm/glm.hpp>
#include <stdexcept>
#include <algorithm>

namespace core
{
	WireframePipelineResult PipelineUtilities::createWireframePipeline(const WireframePipelineConfig& config)
	{
		WireframePipelineResult result{};

		// Push constant range for vertex + fragment stages
		vk::PushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
		pushConstantRange.offset = 0;
		pushConstantRange.size = config.pushConstantSize;

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.setLayoutCount = 0;
		pipelineLayoutInfo.pSetLayouts = nullptr;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

		// RAII: Use unique handle for automatic cleanup on failure
		vk::UniquePipelineLayout uniqueLayout = config.device.createPipelineLayoutUnique(pipelineLayoutInfo);

		// Vertex input - simple vec3 positions
		vk::VertexInputBindingDescription bindingDescription{};
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(glm::vec3);
		bindingDescription.inputRate = vk::VertexInputRate::eVertex;

		vk::VertexInputAttributeDescription attributeDescription{};
		attributeDescription.binding = 0;
		attributeDescription.location = 0;
		attributeDescription.format = vk::Format::eR32G32B32Sfloat;
		attributeDescription.offset = 0;

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = 1;
		vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = vk::PrimitiveTopology::eLineList;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		vk::Viewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(config.extent.width);
		viewport.height = static_cast<float>(config.extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		vk::Rect2D scissor{};
		scissor.offset = vk::Offset2D{0, 0};
		scissor.extent = config.extent;

		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizer.depthBiasEnable = VK_FALSE;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_FALSE;
		depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA;

		if (config.enableBlending)
		{
			colorBlendAttachment.blendEnable = VK_TRUE;
			colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
			colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
			colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
			colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
			colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
			colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
		}
		else
		{
			colorBlendAttachment.blendEnable = VK_FALSE;
		}

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.stageCount = static_cast<uint32_t>(config.shaderStages.size());
		pipelineInfo.pStages = config.shaderStages.data();
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.layout = uniqueLayout.get();
		pipelineInfo.renderPass = config.renderPass;
		pipelineInfo.subpass = 0;

		// RAII: Use unique handle for automatic cleanup on failure
		vk::UniquePipeline uniquePipeline = config.device.createGraphicsPipelineUnique(nullptr, pipelineInfo).value;

		// Success: release ownership to result (caller manages lifetime)
		result.pipelineLayout = uniqueLayout.release();
		result.pipeline = uniquePipeline.release();

		return result;
	}

	GraphicsPipelineResult PipelineUtilities::createGraphicsPipeline(const GraphicsPipelineConfig& config)
	{
		GraphicsPipelineResult result{};

		// RAII: Use unique handle for layout if we create it (nullptr if using existing)
		vk::UniquePipelineLayout uniqueLayout;

		if (config.existingPipelineLayout)
		{
			result.pipelineLayout = config.existingPipelineLayout;
		}
		else
		{
			vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(config.descriptorSetLayouts.size());
			pipelineLayoutInfo.pSetLayouts = config.descriptorSetLayouts.empty() ? nullptr : config.descriptorSetLayouts.data();

			vk::PushConstantRange pushConstantRange{};
			if (config.pushConstantSize > 0)
			{
				pushConstantRange.stageFlags = config.pushConstantStages;
				pushConstantRange.offset = 0;
				pushConstantRange.size = config.pushConstantSize;
				pipelineLayoutInfo.pushConstantRangeCount = 1;
				pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
			}

			uniqueLayout = config.device.createPipelineLayoutUnique(pipelineLayoutInfo);
			result.pipelineLayout = uniqueLayout.get();
		}
		
		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(config.vertexBindings.size());
		vertexInputInfo.pVertexBindingDescriptions = config.vertexBindings.empty() ? nullptr : config.vertexBindings.data();
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(config.vertexAttributes.size());
		vertexInputInfo.pVertexAttributeDescriptions = config.vertexAttributes.empty() ? nullptr : config.vertexAttributes.data();
		
		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = config.topology;
		inputAssembly.primitiveRestartEnable = VK_FALSE;
		
		vk::Viewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(config.extent.width);
		viewport.height = static_cast<float>(config.extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		vk::Rect2D scissor{};
		scissor.offset = vk::Offset2D{0, 0};
		scissor.extent = config.extent;

		bool hasDynamicViewport = std::find(config.dynamicStates.begin(), config.dynamicStates.end(),
			vk::DynamicState::eViewport) != config.dynamicStates.end();
		bool hasDynamicScissor = std::find(config.dynamicStates.begin(), config.dynamicStates.end(),
			vk::DynamicState::eScissor) != config.dynamicStates.end();

		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.pViewports = hasDynamicViewport ? nullptr : &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = hasDynamicScissor ? nullptr : &scissor;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = config.polygonMode;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = config.cullMode;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizer.depthBiasEnable = config.depthBiasEnable ? VK_TRUE : VK_FALSE;
		rasterizer.depthBiasConstantFactor = config.depthBiasConstantFactor;
		rasterizer.depthBiasSlopeFactor = config.depthBiasSlopeFactor;
		rasterizer.depthBiasClamp = 0.0f;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = config.depthTestEnable ? VK_TRUE : VK_FALSE;
		depthStencil.depthWriteEnable = config.depthWriteEnable ? VK_TRUE : VK_FALSE;
		depthStencil.depthCompareOp = config.depthCompareOp;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
		                                      vk::ColorComponentFlagBits::eG |
		                                      vk::ColorComponentFlagBits::eB |
		                                      vk::ColorComponentFlagBits::eA;

		if (config.blendEnable)
		{
			colorBlendAttachment.blendEnable = VK_TRUE;
			colorBlendAttachment.srcColorBlendFactor = config.srcColorBlendFactor;
			colorBlendAttachment.dstColorBlendFactor = config.dstColorBlendFactor;
			colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
			colorBlendAttachment.srcAlphaBlendFactor = config.srcAlphaBlendFactor;
			colorBlendAttachment.dstAlphaBlendFactor = config.dstAlphaBlendFactor;
			colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
		}
		else
		{
			colorBlendAttachment.blendEnable = VK_FALSE;
		}

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		vk::PipelineDynamicStateCreateInfo dynamicStateInfo{};
		if (!config.dynamicStates.empty())
		{
			dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(config.dynamicStates.size());
			dynamicStateInfo.pDynamicStates = config.dynamicStates.data();
		}

		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.stageCount = static_cast<uint32_t>(config.shaderStages.size());
		pipelineInfo.pStages = config.shaderStages.data();
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = config.dynamicStates.empty() ? nullptr : &dynamicStateInfo;
		pipelineInfo.layout = result.pipelineLayout;
		pipelineInfo.renderPass = config.renderPass;
		pipelineInfo.subpass = 0;
		
		vk::UniquePipeline uniquePipeline = config.device.createGraphicsPipelineUnique(nullptr, pipelineInfo).value;

		// Success: release ownership to result (caller manages lifetime)
		if (uniqueLayout)
		{
			uniqueLayout.release();
		}
		result.pipeline = uniquePipeline.release();

		return result;
	}

	MeshShaderPipelineResult PipelineUtilities::createMeshShaderPipeline(const MeshShaderPipelineConfig& config)
	{
		MeshShaderPipelineResult result{};

		// RAII: Use unique handle for layout if we create it (nullptr if using existing)
		vk::UniquePipelineLayout uniqueLayout;

		if (config.existingPipelineLayout)
		{
			result.pipelineLayout = config.existingPipelineLayout;
		}
		else
		{
			vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(config.descriptorSetLayouts.size());
			pipelineLayoutInfo.pSetLayouts = config.descriptorSetLayouts.empty() ? nullptr : config.descriptorSetLayouts.data();

			vk::PushConstantRange pushConstantRange{};
			if (config.pushConstantSize > 0)
			{
				pushConstantRange.stageFlags = config.pushConstantStages;
				pushConstantRange.offset = 0;
				pushConstantRange.size = config.pushConstantSize;
				pipelineLayoutInfo.pushConstantRangeCount = 1;
				pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
			}

			uniqueLayout = config.device.createPipelineLayoutUnique(pipelineLayoutInfo);
			result.pipelineLayout = uniqueLayout.get();
		}
		
		vk::Viewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(config.extent.width);
		viewport.height = static_cast<float>(config.extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		vk::Rect2D scissor{};
		scissor.offset = vk::Offset2D{0, 0};
		scissor.extent = config.extent;

		bool hasDynamicViewport = std::find(config.dynamicStates.begin(), config.dynamicStates.end(),
			vk::DynamicState::eViewport) != config.dynamicStates.end();
		bool hasDynamicScissor = std::find(config.dynamicStates.begin(), config.dynamicStates.end(),
			vk::DynamicState::eScissor) != config.dynamicStates.end();

		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.pViewports = hasDynamicViewport ? nullptr : &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = hasDynamicScissor ? nullptr : &scissor;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = config.polygonMode;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = config.cullMode;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizer.depthBiasEnable = VK_FALSE;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = config.depthTestEnable ? VK_TRUE : VK_FALSE;
		depthStencil.depthWriteEnable = config.depthWriteEnable ? VK_TRUE : VK_FALSE;
		depthStencil.depthCompareOp = config.depthCompareOp;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
		                                      vk::ColorComponentFlagBits::eG |
		                                      vk::ColorComponentFlagBits::eB |
		                                      vk::ColorComponentFlagBits::eA;

		if (config.blendEnable)
		{
			colorBlendAttachment.blendEnable = VK_TRUE;
			colorBlendAttachment.srcColorBlendFactor = config.srcColorBlendFactor;
			colorBlendAttachment.dstColorBlendFactor = config.dstColorBlendFactor;
			colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
			colorBlendAttachment.srcAlphaBlendFactor = config.srcAlphaBlendFactor;
			colorBlendAttachment.dstAlphaBlendFactor = config.dstAlphaBlendFactor;
			colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
		}
		else
		{
			colorBlendAttachment.blendEnable = VK_FALSE;
		}

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.logicOpEnable = VK_FALSE;
		if (!config.colorBlendAttachments.empty())
		{
			colorBlending.attachmentCount = static_cast<uint32_t>(config.colorBlendAttachments.size());
			colorBlending.pAttachments = config.colorBlendAttachments.data();
		}
		else
		{
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;
		}

		vk::PipelineDynamicStateCreateInfo dynamicStateInfo{};
		if (!config.dynamicStates.empty())
		{
			dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(config.dynamicStates.size());
			dynamicStateInfo.pDynamicStates = config.dynamicStates.data();
		}

		// Create mesh shader pipeline
		// Key difference: pVertexInputState and pInputAssemblyState are nullptr
		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.stageCount = static_cast<uint32_t>(config.shaderStages.size());
		pipelineInfo.pStages = config.shaderStages.data();
		pipelineInfo.pVertexInputState = nullptr;    // Not used for mesh shaders
		pipelineInfo.pInputAssemblyState = nullptr;  // Not used for mesh shaders
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = config.dynamicStates.empty() ? nullptr : &dynamicStateInfo;
		pipelineInfo.layout = result.pipelineLayout;
		pipelineInfo.renderPass = config.renderPass;
		pipelineInfo.subpass = 0;

		vk::UniquePipeline uniquePipeline = config.device.createGraphicsPipelineUnique(nullptr, pipelineInfo).value;

		// Success: release ownership to result (caller manages lifetime)
		if (uniqueLayout)
		{
			uniqueLayout.release(); 
		}
		result.pipeline = uniquePipeline.release();

		return result;
	}
}
