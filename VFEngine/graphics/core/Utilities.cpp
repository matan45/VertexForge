#include "Utilities.hpp"
#include "print/Logger.hpp"
#include <cstring>
#include <glm/glm.hpp>

namespace core {

	QueueFamilyIndices Utilities::findQueueFamiliesFromDevice(const vk::PhysicalDevice& device, const vk::SurfaceKHR& surface)
	{
		QueueFamilyIndices indices;
		const std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();

		if (debug) {
			loggerInfo("Found {} queue families.", queueFamilies.size());
		}

		int i = 0;
		for (const vk::QueueFamilyProperties& queueFamily : queueFamilies) {
			if (debug) {
				loggerInfo("Queue Family {}: Graphics: {}, Compute: {}, Transfer: {}",
					i,
					(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) ? "Yes" : "No",
					(queueFamily.queueFlags & vk::QueueFlagBits::eCompute) ? "Yes" : "No",
					(queueFamily.queueFlags & vk::QueueFlagBits::eTransfer) ? "Yes" : "No"
				);
			}

			if (device.getSurfaceSupportKHR(i, surface)) {
				indices.presentFamily = i;
			}

			if ((queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) && (queueFamily.queueFlags & vk::QueueFlagBits::eCompute)) {
				indices.graphicsAndComputeFamily = i;
			}

			// Look for a dedicated transfer queue (transfer-only, no graphics)
			// This allows async transfers without blocking graphics work
			if ((queueFamily.queueFlags & vk::QueueFlagBits::eTransfer) &&
			    !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) &&
			    !indices.transferFamily.has_value()) {
				indices.transferFamily = i;
			}

			i++;
		}

		// If no dedicated transfer queue found, fall back to graphics queue for transfers
		if (!indices.transferFamily.has_value() && indices.graphicsAndComputeFamily.has_value()) {
			indices.transferFamily = indices.graphicsAndComputeFamily;
		}

		if (!indices.isComplete()) {
			if (debug) {
				loggerWarning("Could not find complete queue family support.");
			}
		}

		if (debug && indices.hasDedicatedTransferQueue()) {
			loggerInfo("Found dedicated transfer queue family: {}", indices.transferFamily.value());
		}

		return indices;
	}

	core::SwapchainSupportDetails Utilities::querySwapchainSupport(const vk::PhysicalDevice& device, const vk::SurfaceKHR& surface)
	{
		SwapchainSupportDetails details;
		details.capabilities = device.getSurfaceCapabilitiesKHR(surface);
		details.formats = device.getSurfaceFormatsKHR(surface);
		details.presentModes = device.getSurfacePresentModesKHR(surface);
		return details;
	}

	uint32_t Utilities::findMemoryType(const vk::PhysicalDevice& device, uint32_t typeFilter, vk::MemoryPropertyFlags properties)
	{
		vk::PhysicalDeviceMemoryProperties memProperties = device.getMemoryProperties();

		for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
			if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
				return i;
			}
		}

		loggerError("Failed to find suitable memory type.");
		return 0;
	}

	vk::UniqueCommandBuffer Utilities::beginSingleTimeCommands(const vk::Device& device, const vk::CommandPool& commandPool) {
		vk::CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = commandPool;
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;

		vk::UniqueCommandBuffer commandBuffer;
		try {
			commandBuffer = std::move(device.allocateCommandBuffersUnique(allocInfo).front());
		}
		catch (const vk::SystemError& err) {
			loggerError("Failed to allocate command buffer: {}", err.what());
			throw;
		}

		vk::CommandBufferBeginInfo beginInfo{};
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

		commandBuffer->begin(beginInfo);

		return commandBuffer;
	}

	void Utilities::endSingleTimeCommands(const vk::Queue& queue, const vk::UniqueCommandBuffer& commandBuffer, const vk::Fence& renderFence)
	{
		commandBuffer->end();

		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &(*commandBuffer);

		try {
			queue.submit(submitInfo, renderFence);
			queue.waitIdle();
		}
		catch (const vk::SystemError& err) {
			loggerError("Failed to submit command buffer: {}", err.what());
		}
	}

	void Utilities::transitionImageLayout(const vk::CommandBuffer& commandBuffer, vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageAspectFlags aspectMask, uint32_t layer, uint32_t numMips)
	{
		using enum vk::AccessFlagBits;
		using enum vk::ImageLayout;
		vk::ImageMemoryBarrier barrier{};
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;

		barrier.subresourceRange.aspectMask = aspectMask;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = numMips;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = layer;

		vk::PipelineStageFlags sourceStage;
		vk::PipelineStageFlags destinationStage;

		if (oldLayout == eUndefined && newLayout == eColorAttachmentOptimal) {
			barrier.srcAccessMask = eNoneKHR;
			barrier.dstAccessMask = eColorAttachmentWrite;
			sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
			destinationStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

		}
		else if (oldLayout == eColorAttachmentOptimal && newLayout == eShaderReadOnlyOptimal) {
			barrier.srcAccessMask = eColorAttachmentWrite;
			barrier.dstAccessMask = eShaderRead;
			sourceStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			destinationStage = vk::PipelineStageFlagBits::eFragmentShader;

		}
		else if (oldLayout == eShaderReadOnlyOptimal && newLayout == eTransferSrcOptimal) {
			barrier.srcAccessMask = eShaderRead;
			barrier.dstAccessMask = eTransferRead;
			sourceStage = vk::PipelineStageFlagBits::eFragmentShader;
			destinationStage = vk::PipelineStageFlagBits::eTransfer;
		}

		else if (oldLayout == eUndefined && newLayout == eDepthStencilAttachmentOptimal) {
			barrier.srcAccessMask = eNone;
			barrier.dstAccessMask = eDepthStencilAttachmentWrite;
			sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
			destinationStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;

		}
		else if (oldLayout == eDepthStencilAttachmentOptimal && newLayout == eShaderReadOnlyOptimal) {
			barrier.srcAccessMask = eDepthStencilAttachmentWrite;
			barrier.dstAccessMask = eShaderRead;
			sourceStage = vk::PipelineStageFlagBits::eLateFragmentTests;
			destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else if (oldLayout == eTransferSrcOptimal && newLayout == eColorAttachmentOptimal) {
			barrier.srcAccessMask = eTransferRead;
			barrier.dstAccessMask = eColorAttachmentWrite;
			sourceStage = vk::PipelineStageFlagBits::eTransfer;
			destinationStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		}

		else if (oldLayout == eUndefined && newLayout == eShaderReadOnlyOptimal) {
			barrier.srcAccessMask = eNone;
			barrier.dstAccessMask = eShaderRead;
			sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
			destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else if (oldLayout == eUndefined && newLayout == eTransferDstOptimal) {
			barrier.srcAccessMask = eNone;
			barrier.dstAccessMask = eTransferWrite;
			sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
			destinationStage = vk::PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == eColorAttachmentOptimal && newLayout == eTransferSrcOptimal) {
			barrier.srcAccessMask = eColorAttachmentWrite;
			barrier.dstAccessMask = eTransferRead;
			sourceStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			destinationStage = vk::PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == eTransferDstOptimal && newLayout == eShaderReadOnlyOptimal) {
			barrier.srcAccessMask = eTransferWrite;
			barrier.dstAccessMask = eShaderRead;
			sourceStage = vk::PipelineStageFlagBits::eTransfer;
			destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
		}

		commandBuffer.pipelineBarrier(
			sourceStage, destinationStage,
			vk::DependencyFlags{},
			nullptr, nullptr, barrier
		);
	}

	void Utilities::createBuffer(const BufferInfoRequest& bufferInfo, vk::Buffer& buffer, vk::DeviceMemory& bufferMemory)
	{
		vk::BufferCreateInfo bufferCreateInfo{};
		bufferCreateInfo.size = bufferInfo.size;
		bufferCreateInfo.usage = bufferInfo.usage;
		bufferCreateInfo.sharingMode = vk::SharingMode::eExclusive;

		buffer = bufferInfo.logicalDevice.createBuffer(bufferCreateInfo);

		vk::MemoryRequirements memRequirements = bufferInfo.logicalDevice.getBufferMemoryRequirements(buffer);
		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = core::Utilities::findMemoryType(
			bufferInfo.physicalDevice,
			memRequirements.memoryTypeBits,
			bufferInfo.properties
		);

		bufferMemory = bufferInfo.logicalDevice.allocateMemory(allocInfo);
		bufferInfo.logicalDevice.bindBufferMemory(buffer, bufferMemory, 0);
	}

	void Utilities::createImage(const ImageInfoRequest& imageInfo, vk::Image& image, vk::DeviceMemory& imageMemory)
	{
		vk::ImageCreateInfo imageCreateInfo{};
		imageCreateInfo.imageType = vk::ImageType::e2D;
		imageCreateInfo.extent.width = imageInfo.width;
		imageCreateInfo.extent.height = imageInfo.height;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = imageInfo.mipLevels;
		imageCreateInfo.arrayLayers = imageInfo.layers;
		imageCreateInfo.format = imageInfo.format;
		imageCreateInfo.tiling = imageInfo.tiling;
		imageCreateInfo.initialLayout = vk::ImageLayout::eUndefined;
		imageCreateInfo.usage = imageInfo.usage;
		imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
		imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
		imageCreateInfo.flags = imageInfo.imageFlags;

		image = imageInfo.logicalDevice.createImage(imageCreateInfo);

		vk::MemoryRequirements memRequirements = imageInfo.logicalDevice.getImageMemoryRequirements(image);
		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = core::Utilities::findMemoryType(imageInfo.physicalDevice, memRequirements.memoryTypeBits, imageInfo.properties);

		imageMemory = imageInfo.logicalDevice.allocateMemory(allocInfo);
		imageInfo.logicalDevice.bindImageMemory(image, imageMemory, 0);
	}

	void Utilities::createImageView(const ImageViewInfoRequest& imageInfoView, vk::ImageView& imageView)
	{
		vk::ImageViewCreateInfo viewInfo{};
		viewInfo.image = imageInfoView.image;
		viewInfo.viewType = imageInfoView.imageType;
		viewInfo.format = imageInfoView.format;
		viewInfo.subresourceRange.aspectMask = imageInfoView.aspectFlags;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = imageInfoView.mipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = imageInfoView.layerCount;

		imageView = imageInfoView.logicalDevice.createImageView(viewInfo);
	}

	void Utilities::copyToBuffer(
		const vk::Device& device,
		const vk::PhysicalDevice& physicalDevice,
		const vk::Queue& queue,
		const vk::CommandPool& commandPool,
		vk::Buffer dstBuffer,
		const void* srcData,
		vk::DeviceSize size,
		vk::DeviceSize offset)
	{
		if (size == 0 || srcData == nullptr) {
			return;
		}

		// Create staging buffer with host-visible memory
		vk::Buffer stagingBuffer;
		vk::DeviceMemory stagingMemory;

		BufferInfoRequest stagingInfo(
			device,
			physicalDevice,
			size,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		);
		createBuffer(stagingInfo, stagingBuffer, stagingMemory);
		
		void* mappedData = device.mapMemory(stagingMemory, 0, size, {});
		std::memcpy(mappedData, srcData, static_cast<size_t>(size));
		device.unmapMemory(stagingMemory);

		// Copy from staging to destination buffer with offset
		// Use try/catch to ensure staging resources are cleaned up on failure
		try {
			auto cmd = beginSingleTimeCommands(device, commandPool);
			vk::BufferCopy copyRegion{ 0, offset, size };
			cmd->copyBuffer(stagingBuffer, dstBuffer, copyRegion);
			endSingleTimeCommands(queue, cmd);
		} catch (...) {
			device.destroyBuffer(stagingBuffer);
			device.freeMemory(stagingMemory);
			throw;
		}

		// Cleanup staging buffer
		device.destroyBuffer(stagingBuffer);
		device.freeMemory(stagingMemory);
	}

	WireframePipelineResult Utilities::createWireframePipeline(const WireframePipelineConfig& config)
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

		result.pipelineLayout = config.device.createPipelineLayout(pipelineLayoutInfo);

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
		pipelineInfo.layout = result.pipelineLayout;
		pipelineInfo.renderPass = config.renderPass;
		pipelineInfo.subpass = 0;

		auto createResult = config.device.createGraphicsPipeline(nullptr, pipelineInfo);
		if (createResult.result != vk::Result::eSuccess)
		{
			config.device.destroyPipelineLayout(result.pipelineLayout);
			throw std::runtime_error("Failed to create wireframe graphics pipeline");
		}
		result.pipeline = createResult.value;

		return result;
	}

	GraphicsPipelineResult Utilities::createGraphicsPipeline(const GraphicsPipelineConfig& config)
	{
		GraphicsPipelineResult result{};

		// Use existing pipeline layout or create a new one
		bool createdLayout = false;
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

			result.pipelineLayout = config.device.createPipelineLayout(pipelineLayoutInfo);
			createdLayout = true;
		}

		// Vertex input
		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(config.vertexBindings.size());
		vertexInputInfo.pVertexBindingDescriptions = config.vertexBindings.empty() ? nullptr : config.vertexBindings.data();
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(config.vertexAttributes.size());
		vertexInputInfo.pVertexAttributeDescriptions = config.vertexAttributes.empty() ? nullptr : config.vertexAttributes.data();

		// Input assembly
		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = config.topology;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		// Viewport and scissor
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

		// Rasterization
		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = config.polygonMode;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = config.cullMode;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizer.depthBiasEnable = VK_FALSE;

		// Multisampling
		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		// Depth stencil
		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = config.depthTestEnable ? VK_TRUE : VK_FALSE;
		depthStencil.depthWriteEnable = config.depthWriteEnable ? VK_TRUE : VK_FALSE;
		depthStencil.depthCompareOp = config.depthCompareOp;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		// Color blending
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

		// Create pipeline
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
		pipelineInfo.layout = result.pipelineLayout;
		pipelineInfo.renderPass = config.renderPass;
		pipelineInfo.subpass = 0;

		auto createResult = config.device.createGraphicsPipeline(nullptr, pipelineInfo);
		if (createResult.result != vk::Result::eSuccess)
		{
			// Only destroy layout if we created it
			if (createdLayout)
			{
				config.device.destroyPipelineLayout(result.pipelineLayout);
			}
			throw std::runtime_error("Failed to create graphics pipeline");
		}
		result.pipeline = createResult.value;

		return result;
	}

	void Utilities::destroyBuffer(const vk::Device& device, vk::Buffer& buffer, vk::DeviceMemory& memory)
	{
		if (buffer) {
			device.destroyBuffer(buffer);
			buffer = nullptr;
		}
		if (memory) {
			device.freeMemory(memory);
			memory = nullptr;
		}
	}

}

