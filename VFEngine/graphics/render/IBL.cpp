#include "IBL.hpp"
#include "../core/Utilities.hpp"
#include "print/Logger.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include <memory>


namespace render
{
	IBL::IBL(core::Device& device, core::SwapChain& swapChain,
		core::OffscreenResources& offscreenResources) : device{ device },
		swapChain{ swapChain },
		offscreenResources{ offscreenResources }
	{
		shaderIrradianceCube = std::make_shared<core::Shader>(device);
		brdfLUTShader = std::make_shared<core::Shader>(device);
		prefilterShader = std::make_shared<core::Shader>(device);
		skybox.skyboxShader = std::make_shared<core::Shader>(device);

		skybox.skyboxShader->readShader("../../resources/shaders/ibl/skybox.glsl");
		shaderIrradianceCube->readShader("../../resources/shaders/ibl/equirectangular_convolution.glsl");
		brdfLUTShader->readShader("../../resources/shaders/ibl/brdf.glsl");
		prefilterShader->readShader("../../resources/shaders/ibl/prefilter.glsl");

		vk::CommandPoolCreateInfo commandPoolInfo;
		commandPoolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
		commandPoolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
		commandPool = device.getLogicalDevice().createCommandPoolUnique(commandPoolInfo);
	}

	void IBL::recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
	{
		if (isDisplay && camera)
		{
			// Debug: Log view matrix values to verify camera is updating
			static int frameCount = 0;
			if (frameCount++ % 60 == 0) { // Log every 60 frames
				loggerInfo("IBL Camera viewMatrix[0][0]={}, [1][1]={}, [2][2]={}",
					camera->viewMatrix[0][0], camera->viewMatrix[1][1], camera->viewMatrix[2][2]);
			}
			updateUniformBuffer(camera->viewMatrix, camera->projectionMatrix, skybox.uniformBufferMemory);
			vk::RenderPassBeginInfo renderPassInfo{};
			renderPassInfo.renderPass = skybox.renderPass;
			renderPassInfo.framebuffer = skybox.framebuffers[imageIndex];
			renderPassInfo.renderArea.offset.x = 0;
			renderPassInfo.renderArea.offset.y = 0;
			renderPassInfo.renderArea.extent = swapChain.getSwapchainExtent();

			std::array<vk::ClearValue, 1> clearValues{};
			clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
			renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
			renderPassInfo.pClearValues = clearValues.data();

			// Begin render pass
			commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

			// Bind the graphics pipeline
			commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, skybox.graphicsPipeline);
			commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, skybox.pipelineLayout, 0,
				skybox.descriptorSet,
				{});

			// Bind vertex buffer
			vk::DeviceSize offsets[] = { 0 };
			commandBuffer.bindVertexBuffers(0, skybox.vertexBuffer, offsets);
			commandBuffer.draw(static_cast<uint32_t>(cubeVertices.size()), 1, 0, 0);

			// End render pass
			commandBuffer.endRenderPass();
		}
	}

	void IBL::init(std::string_view path)
	{
		hdrTexture = std::make_shared<core::Texture>(device);
		hdrTexture->loadHDRFromFile(path, vk::Format::eR32G32B32Sfloat, false);

		generateIrradianceCube();
		generateBRDFLUT();
		generatePrefilteredCube();
		drawCube();
	}

	void IBL::recreate()
	{
	}

	void IBL::remove()
	{
		if (isDisplay) {
			isDisplay = false;
			hdrTexture.reset();

			for (auto const& frame : skybox.framebuffers)
			{
				device.getLogicalDevice().destroyFramebuffer(frame);
			}
			device.getLogicalDevice().destroyBuffer(skybox.vertexBuffer);
			device.getLogicalDevice().freeMemory(skybox.vertexBufferMemory);

			device.getLogicalDevice().destroyBuffer(skybox.uniformBuffer);
			device.getLogicalDevice().freeMemory(skybox.uniformBufferMemory);

			device.getLogicalDevice().destroyRenderPass(skybox.renderPass);
			device.getLogicalDevice().destroyPipeline(skybox.graphicsPipeline);
			device.getLogicalDevice().destroyPipelineLayout(skybox.pipelineLayout);
			device.getLogicalDevice().freeDescriptorSets(skybox.descriptorPool, skybox.descriptorSet);
			device.getLogicalDevice().destroyDescriptorPool(skybox.descriptorPool);
			device.getLogicalDevice().destroyDescriptorSetLayout(skybox.descriptorSetLayout);

			cleanUpImage(imageIrradianceCube);
			cleanUpImage(brdfLUTImage);
			cleanUpImage(prefilterImage);
		}

	}

	void IBL::cleanUp()
	{
		device.getLogicalDevice().waitIdle();
		skybox.skyboxShader->cleanUp();
		shaderIrradianceCube->cleanUp();
		brdfLUTShader->cleanUp();
		prefilterShader->cleanUp();
		commandPool.reset();
		remove();

	}

	void IBL::generatePrefilteredCube()
	{
		const uint32_t mipLevels = static_cast<uint32_t>(floor(log2(CUBE_MAP_SIZE))) + 1;
#pragma region ImageAndSamplerCreate
		core::ImageInfoRequest cubeMapImageRequest(device.getLogicalDevice(), device.getPhysicalDevice());
		cubeMapImageRequest.format = vk::Format::eR16G16B16A16Sfloat;
		cubeMapImageRequest.layers = 6;
		cubeMapImageRequest.mipLevels = mipLevels;
		cubeMapImageRequest.width = CUBE_MAP_SIZE;
		cubeMapImageRequest.height = CUBE_MAP_SIZE;
		cubeMapImageRequest.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled |
			vk::ImageUsageFlagBits::eColorAttachment;
		cubeMapImageRequest.imageFlags = vk::ImageCreateFlagBits::eCubeCompatible;
		core::Utilities::createImage(cubeMapImageRequest, prefilterImage.image,
			prefilterImage.imageMemory);

		core::ImageViewInfoRequest cubeMapImageViewRequest(device.getLogicalDevice(), prefilterImage.image);
		cubeMapImageViewRequest.format = vk::Format::eR16G16B16A16Sfloat;
		cubeMapImageViewRequest.layerCount = 6;
		cubeMapImageViewRequest.mipLevels = mipLevels;
		cubeMapImageViewRequest.imageType = vk::ImageViewType::eCube;
		core::Utilities::createImageView(cubeMapImageViewRequest, prefilterImage.imageView);

		vk::SamplerCreateInfo samplerInfo;
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.anisotropyEnable = VK_TRUE;
		samplerInfo.maxAnisotropy = 16;
		samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		prefilterImage.sampler = device.getLogicalDevice().createSampler(samplerInfo);
#pragma endregion ImageAndSamplerCreate
#pragma region RenderPass

		vk::AttachmentDescription colorAttachment{};
		colorAttachment.format = vk::Format::eR16G16B16A16Sfloat;
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		colorAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::AttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		// Use subpass dependencies for layout transitions
		std::array<vk::SubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
			vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
			vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		vk::RenderPassCreateInfo renderPassInfo{};
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 2;
		renderPassInfo.pDependencies = dependencies.data();

		vk::RenderPass renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
#pragma endregion RenderPass
#pragma region VertexBuffer
		//DEFINE THE VERTEX BUFFER LAYOUT
		vk::VertexInputBindingDescription vertexInputBindingDescription;
		vertexInputBindingDescription.binding = 0;
		vertexInputBindingDescription.stride = sizeof(glm::vec3);
		vertexInputBindingDescription.inputRate = vk::VertexInputRate::eVertex;

		std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes;
		vertexInputAttributes.push_back({ 0, 0, vk::Format::eR32G32B32Sfloat, 0 });

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &vertexInputBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributes.data();

		vk::Buffer cubeVertexBuffer;
		vk::DeviceMemory cubeVertexBufferMemory;

		core::BufferInfoRequest cubeVertexBufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
		cubeVertexBufferInfo.size = sizeof(cubeVertices[0]) * cubeVertices.size();
		cubeVertexBufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer;
		cubeVertexBufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent;
		core::Utilities::createBuffer(cubeVertexBufferInfo, cubeVertexBuffer, cubeVertexBufferMemory);

		void* data;
		if (vk::Result result = device.getLogicalDevice().mapMemory(cubeVertexBufferMemory, 0,
			cubeVertexBufferInfo.size, {},
			&data); result != vk::Result::eSuccess)
		{
			loggerError("failed to map memory");
		}
		memcpy(data, cubeVertices.data(), cubeVertexBufferInfo.size);
		device.getLogicalDevice().unmapMemory(cubeVertexBufferMemory);

#pragma endregion VertexBuffer
#pragma region UniformBuffer
		//DEFINE THE UNIFORM BUFFER LAYOUT
		vk::DescriptorPool descriptorPool;

		std::vector<vk::DescriptorPoolSize> poolSizes(2);
		poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
		poolSizes[0].descriptorCount = 1;
		poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
		poolSizes[1].descriptorCount = 1;

		vk::DescriptorPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = 1;

		descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);

		vk::DescriptorSetLayout descriptorSetLayout;

		std::vector<vk::DescriptorSetLayoutBinding> bindings(2);

		bindings[0].binding = 0;
		bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;
		bindings[0].pImmutableSamplers = nullptr;

		bindings[1].binding = 1;
		bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
		bindings[1].pImmutableSamplers = nullptr;

		vk::DescriptorSetLayoutCreateInfo layoutInfo;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);

		//DEFINE THE UNIFORM BUFFER AND SAMPLER
		vk::DescriptorSet descriptorSet;

		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &descriptorSetLayout;

		descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

		vk::DescriptorImageInfo cubeImageInfo;
		cubeImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		cubeImageInfo.imageView = imageIrradianceCube.imageView;
		cubeImageInfo.sampler = imageIrradianceCube.sampler;

		vk::WriteDescriptorSet descriptorWriteCube;
		descriptorWriteCube.dstSet = descriptorSet;
		descriptorWriteCube.dstBinding = 1;
		descriptorWriteCube.dstArrayElement = 0;
		descriptorWriteCube.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		descriptorWriteCube.descriptorCount = 1;
		descriptorWriteCube.pImageInfo = &cubeImageInfo;

		device.getLogicalDevice().updateDescriptorSets(descriptorWriteCube, nullptr);

		vk::Buffer uboUniformBuffer;
		vk::DeviceMemory uboUniformBufferMemory;
		core::BufferInfoRequest uboBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
		uboBufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
		uboBufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent;
		uboBufferRequest.size = sizeof(UniformBufferObject);
		core::Utilities::createBuffer(uboBufferRequest, uboUniformBuffer, uboUniformBufferMemory);

		vk::DescriptorBufferInfo uboBufferInfo;
		uboBufferInfo.buffer = uboUniformBuffer;
		uboBufferInfo.offset = 0;
		uboBufferInfo.range = sizeof(UniformBufferObject);

		vk::WriteDescriptorSet descriptorWriteUbo;
		descriptorWriteUbo.dstSet = descriptorSet;
		descriptorWriteUbo.dstBinding = 0;
		descriptorWriteUbo.dstArrayElement = 0;
		descriptorWriteUbo.descriptorType = vk::DescriptorType::eUniformBuffer;
		descriptorWriteUbo.descriptorCount = 1;
		descriptorWriteUbo.pBufferInfo = &uboBufferInfo;

		device.getLogicalDevice().updateDescriptorSets(descriptorWriteUbo, nullptr);

#pragma endregion UniformBuffer
#pragma region GraphicsPipline

		vk::PushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(float);

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		vk::Viewport viewport;
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(CUBE_MAP_SIZE);
		viewport.height = static_cast<float>(CUBE_MAP_SIZE);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		vk::Rect2D scissor;
		scissor.offset = vk::Offset2D(0, 0);
		scissor.extent = vk::Extent2D(CUBE_MAP_SIZE, CUBE_MAP_SIZE);

		vk::PipelineViewportStateCreateInfo viewportState;
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;

		vk::PipelineRasterizationStateCreateInfo rasterizer;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eBack;
		rasterizer.frontFace = vk::FrontFace::eClockwise;
		rasterizer.depthBiasEnable = VK_FALSE;

		vk::PipelineMultisampleStateCreateInfo multisampling;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment;
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
			| vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		colorBlendAttachment.blendEnable = VK_FALSE;

		vk::PipelineColorBlendStateCreateInfo colorBlending;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
		vk::PipelineLayout pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

		std::array<vk::DynamicState, 2> dynamicStateEnables = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
		vk::PipelineDynamicStateCreateInfo dynamicState;
		dynamicState.pDynamicStates = dynamicStateEnables.data();
		dynamicState.dynamicStateCount = 2;

		vk::GraphicsPipelineCreateInfo pipelineInfo;
		pipelineInfo.stageCount = static_cast<uint32_t>(prefilterShader->getShaderStages().size());
		pipelineInfo.pStages = prefilterShader->getShaderStages().data();
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;
		pipelineInfo.pDynamicState = &dynamicState;

		vk::Pipeline graphicsPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;
#pragma endregion GraphicsPipline
#pragma region ImageHelper
		OffScreenHelper imageHelper;

		core::ImageInfoRequest imageRequest(device.getLogicalDevice(), device.getPhysicalDevice());
		imageRequest.format = vk::Format::eR16G16B16A16Sfloat;
		imageRequest.width = CUBE_MAP_SIZE;
		imageRequest.height = CUBE_MAP_SIZE;
		imageRequest.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
		core::Utilities::createImage(imageRequest, imageHelper.image, imageHelper.memory);

		core::ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), imageHelper.image);
		imageViewRequest.format = vk::Format::eR16G16B16A16Sfloat;
		core::Utilities::createImageView(imageViewRequest, imageHelper.view);

		vk::FramebufferCreateInfo framebufferInfo{};
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &imageHelper.view;
		framebufferInfo.width = CUBE_MAP_SIZE;
		framebufferInfo.height = CUBE_MAP_SIZE;
		framebufferInfo.layers = 1;

		imageHelper.framebuffer = device.getLogicalDevice().createFramebuffer(framebufferInfo);
#pragma endregion ImageHelper
#pragma region ImageTransition
		//transition helper image layout
		vk::UniqueCommandBuffer commandBufferInitHelperImageTransition = core::Utilities::beginSingleTimeCommands(
			device.getLogicalDevice(), commandPool.get());

		core::Utilities::transitionImageLayout(commandBufferInitHelperImageTransition.get(), imageHelper.image,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageAspectFlagBits::eColor);

		core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandBufferInitHelperImageTransition);

		//transition cube image layout
		vk::UniqueCommandBuffer commandBufferInitCubeImage = core::Utilities::beginSingleTimeCommands(
			device.getLogicalDevice(), commandPool.get());

		core::Utilities::transitionImageLayout(commandBufferInitCubeImage.get(), prefilterImage.image,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageAspectFlagBits::eColor, 6, mipLevels);

		core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandBufferInitCubeImage);
#pragma endregion ImageTransition
#pragma region Draw

		vk::UniqueCommandBuffer commandBufferDraw = core::Utilities::beginSingleTimeCommands(
			device.getLogicalDevice(), commandPool.get());

		vk::ClearValue clearColor{ std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f} };

		vk::RenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.framebuffer = imageHelper.framebuffer;
		renderPassBeginInfo.renderArea = vk::Rect2D({ 0, 0 }, { CUBE_MAP_SIZE, CUBE_MAP_SIZE });
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = &clearColor;

		commandBufferDraw.get().setScissor(0, 1, &scissor);
		float roughness = 0;
		for (uint32_t m = 0; m < mipLevels; m++)
		{
			roughness = (float)m / (float)(mipLevels - 1);
			for (uint32_t face = 0; face < 6; face++)
			{
				viewport.width = static_cast<float>(CUBE_MAP_SIZE * std::pow(0.5f, m));
				viewport.height = static_cast<float>(CUBE_MAP_SIZE * std::pow(0.5f, m));
				commandBufferDraw.get().setViewport(0, 1, &viewport);

				commandBufferDraw.get().pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0,
					sizeof(float), &roughness);
				updateUniformBuffer(CameraViewMatrix::captureViews[face], CameraViewMatrix::captureProjection,
					uboUniformBufferMemory);

				// Begin render pass and render to the specific cube face
				commandBufferDraw.get().beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

				// Bind pipeline, descriptor sets, and draw commands
				commandBufferDraw.get().bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
				commandBufferDraw.get().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0,
					descriptorSet,
					{});

				// Bind vertex buffer
				vk::DeviceSize offsets[] = { 0 };
				commandBufferDraw.get().bindVertexBuffers(0, cubeVertexBuffer, offsets);
				commandBufferDraw.get().draw(static_cast<uint32_t>(cubeVertices.size()), 1, 0, 0);

				commandBufferDraw.get().endRenderPass();

				// Ensure synchronization between rendering and copying by transitioning the image layout
				core::Utilities::transitionImageLayout(commandBufferDraw.get(), imageHelper.image,
					vk::ImageLayout::eColorAttachmentOptimal,
					vk::ImageLayout::eTransferSrcOptimal,
					vk::ImageAspectFlagBits::eColor);

				// Set up the copy region for the transfer operation
				vk::ImageCopy copyRegion = {};
				copyRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
				copyRegion.srcSubresource.baseArrayLayer = 0;
				copyRegion.srcSubresource.mipLevel = 0;
				copyRegion.srcSubresource.layerCount = 1;
				copyRegion.srcOffset = vk::Offset3D{ 0, 0, 0 };

				copyRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
				copyRegion.dstSubresource.baseArrayLayer = face;
				copyRegion.dstSubresource.mipLevel = m; // Set mip level to 0 for each face
				copyRegion.dstSubresource.layerCount = 1;
				copyRegion.dstOffset = vk::Offset3D{ 0, 0, 0 };

				copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
				copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
				copyRegion.extent.depth = 1;

				// Copy the image from the framebuffer to the cube map face
				commandBufferDraw.get().copyImage(imageHelper.image, vk::ImageLayout::eTransferSrcOptimal,
					prefilterImage.image,
					vk::ImageLayout::eTransferDstOptimal, 1, &copyRegion);

				// Transition the image back to color attachment layout for the next face
				core::Utilities::transitionImageLayout(commandBufferDraw.get(), imageHelper.image,
					vk::ImageLayout::eTransferSrcOptimal,
					vk::ImageLayout::eColorAttachmentOptimal,
					vk::ImageAspectFlagBits::eColor);
			}
		}

		// Create a fence to wait for completion
		vk::Fence renderFence = device.getLogicalDevice().createFence({});
		core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandBufferDraw, renderFence);

		// Wait for the command buffer to finish executing
		if (vk::Result result = device.getLogicalDevice().waitForFences(renderFence, VK_TRUE, UINT64_MAX); result !=
			vk::Result::eSuccess)
		{
			loggerError("Failed to to wait for Fence IBL:");
		}
#pragma endregion Draw
#pragma region EndImageTransition
		// Begin a new single-time command buffer for the final layout transition
		vk::UniqueCommandBuffer commandBufferEndTransition = core::Utilities::beginSingleTimeCommands(
			device.getLogicalDevice(), commandPool.get());

		core::Utilities::transitionImageLayout(commandBufferEndTransition.get(), prefilterImage.image,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::ImageAspectFlagBits::eColor, 6, mipLevels);

		// End the layout transition command buffer
		core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandBufferEndTransition);
#pragma endregion EndImageTransition
#pragma region CleanUp
		device.getLogicalDevice().destroyFence(renderFence);

		device.getLogicalDevice().destroyBuffer(cubeVertexBuffer);
		device.getLogicalDevice().freeMemory(cubeVertexBufferMemory);

		device.getLogicalDevice().destroyBuffer(uboUniformBuffer);
		device.getLogicalDevice().freeMemory(uboUniformBufferMemory);

		device.getLogicalDevice().destroyFramebuffer(imageHelper.framebuffer);
		device.getLogicalDevice().freeMemory(imageHelper.memory);
		device.getLogicalDevice().destroyImageView(imageHelper.view);
		device.getLogicalDevice().destroyImage(imageHelper.image);

		device.getLogicalDevice().destroyRenderPass(renderPass);
		device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
		device.getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);
		device.getLogicalDevice().destroyPipeline(graphicsPipeline);
		device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
#pragma endregion CleanUp
	}

	void IBL::drawCube()
	{
#pragma region RenderPass
		vk::AttachmentDescription colorAttachment{};
		colorAttachment.format = swapChain.getSwapchainImageFormat();
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::AttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		vk::RenderPassCreateInfo renderPassInfo{};
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;

		skybox.renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
#pragma endregion RenderPass
#pragma region VertexBuffer
		//DEFINE THE VERTEX BUFFER LAYOUT
		std::vector<vk::VertexInputBindingDescription> vertexInputBindingDescriptiones;
		vk::VertexInputBindingDescription vertexInputBindingDescription1;
		vertexInputBindingDescription1.binding = 0;
		vertexInputBindingDescription1.stride = sizeof(glm::vec3);
		vertexInputBindingDescription1.inputRate = vk::VertexInputRate::eVertex;
		vertexInputBindingDescriptiones.push_back(vertexInputBindingDescription1);

		std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes;
		vertexInputAttributes.push_back({ 0, 0, vk::Format::eR32G32B32Sfloat, 0 });

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = vertexInputBindingDescriptiones.data();
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributes.data();

		//DEFINE THE VERTEX BUFFER
		core::BufferInfoRequest vertexCubeVerticesBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
		vertexCubeVerticesBufferRequest.size = sizeof(cubeVertices[0]) * cubeVertices.size();
		vertexCubeVerticesBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
		vertexCubeVerticesBufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent;
		core::Utilities::createBuffer(vertexCubeVerticesBufferRequest, skybox.vertexBuffer, skybox.vertexBufferMemory);

		void* data;
		if (vk::Result result = device.getLogicalDevice().mapMemory(skybox.vertexBufferMemory, 0,
			vertexCubeVerticesBufferRequest.size, {},
			&data); result != vk::Result::eSuccess)
		{
			loggerError("failed to map memory");
		}
		memcpy(data, cubeVertices.data(), vertexCubeVerticesBufferRequest.size);
		device.getLogicalDevice().unmapMemory(skybox.vertexBufferMemory);
#pragma endregion VertexBuffer
#pragma region UniformBuffer
		//DEFINE THE UNIFORM BUFFER LAYOUT
		std::vector<vk::DescriptorPoolSize> poolSizes(2);
		poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
		poolSizes[0].descriptorCount = 1;
		poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
		poolSizes[1].descriptorCount = 1;

		vk::DescriptorPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = 1;

		skybox.descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);

		std::vector<vk::DescriptorSetLayoutBinding> bindings(2);

		bindings[0].binding = 0;
		bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;
		bindings[0].pImmutableSamplers = nullptr;

		bindings[1].binding = 1;
		bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
		bindings[1].pImmutableSamplers = nullptr;

		vk::DescriptorSetLayoutCreateInfo layoutInfo;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		skybox.descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);

		//DEFINE THE UNIFORM BUFFER AND SAMPLER
		core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
		bufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
		bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent;
		bufferRequest.size = sizeof(UniformBufferObject);
		core::Utilities::createBuffer(bufferRequest, skybox.uniformBuffer, skybox.uniformBufferMemory);

		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = skybox.descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &skybox.descriptorSetLayout;

		skybox.descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

		vk::DescriptorImageInfo imageInfo;
		imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		imageInfo.imageView = imageIrradianceCube.imageView;
		imageInfo.sampler = imageIrradianceCube.sampler;

		vk::WriteDescriptorSet descriptorWriteImageSampler;
		descriptorWriteImageSampler.dstSet = skybox.descriptorSet;
		descriptorWriteImageSampler.dstBinding = 1;
		descriptorWriteImageSampler.dstArrayElement = 0;
		descriptorWriteImageSampler.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		descriptorWriteImageSampler.descriptorCount = 1;
		descriptorWriteImageSampler.pImageInfo = &imageInfo;

		device.getLogicalDevice().updateDescriptorSets(descriptorWriteImageSampler, nullptr);

		vk::DescriptorBufferInfo uboBufferInfo;
		uboBufferInfo.buffer = skybox.uniformBuffer;
		uboBufferInfo.offset = 0;
		uboBufferInfo.range = sizeof(UniformBufferObject);

		vk::WriteDescriptorSet descriptorWriteUbo;
		descriptorWriteUbo.dstSet = skybox.descriptorSet;
		descriptorWriteUbo.dstBinding = 0;
		descriptorWriteUbo.dstArrayElement = 0;
		descriptorWriteUbo.descriptorType = vk::DescriptorType::eUniformBuffer;
		descriptorWriteUbo.descriptorCount = 1;
		descriptorWriteUbo.pBufferInfo = &uboBufferInfo;

		device.getLogicalDevice().updateDescriptorSets(descriptorWriteUbo, nullptr);
#pragma endregion UniformBuffer
#pragma region GraphicsPipline
		//DEFINE GRAPHICS PIPELINE
		vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		vk::Viewport viewport;
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(swapChain.getSwapchainExtent().width);
		viewport.height = static_cast<float>(swapChain.getSwapchainExtent().height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		vk::Rect2D scissor;
		scissor.offset = vk::Offset2D(0, 0);
		scissor.extent = vk::Extent2D(swapChain.getSwapchainExtent().width, swapChain.getSwapchainExtent().height);

		vk::PipelineViewportStateCreateInfo viewportState;
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;

		vk::PipelineRasterizationStateCreateInfo rasterizer;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;  // No culling for skybox
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizer.depthBiasEnable = VK_FALSE;

		vk::PipelineMultisampleStateCreateInfo multisampling;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment;
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
			| vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		colorBlendAttachment.blendEnable = VK_FALSE;

		vk::PipelineColorBlendStateCreateInfo colorBlending;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &skybox.descriptorSetLayout;
		skybox.pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

		vk::GraphicsPipelineCreateInfo pipelineInfo;
		pipelineInfo.stageCount = static_cast<uint32_t>(skybox.skyboxShader->getShaderStages().size());
		pipelineInfo.pStages = skybox.skyboxShader->getShaderStages().data();
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.layout = skybox.pipelineLayout;
		pipelineInfo.renderPass = skybox.renderPass;
		pipelineInfo.subpass = 0;

		skybox.graphicsPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;
#pragma endregion GraphicsPipline
#pragma region FrameBuffers
		skybox.framebuffers.resize(offscreenResources.colorImages.size());

		for (uint32_t i = 0; i < skybox.framebuffers.size(); i++)
		{
			vk::ImageView viewImage = offscreenResources.colorImages[i].colorImageView;

			vk::FramebufferCreateInfo framebufferInfo{};
			framebufferInfo.renderPass = skybox.renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = &viewImage;
			framebufferInfo.width = swapChain.getSwapchainExtent().width;
			framebufferInfo.height = swapChain.getSwapchainExtent().height;
			framebufferInfo.layers = 1;

			skybox.framebuffers[i] = device.getLogicalDevice().createFramebuffer(framebufferInfo);
		}
#pragma endregion FrameBuffers
	}

	void IBL::generateIrradianceCube()
	{
#pragma region ImageAndSamplerCreate
		core::ImageInfoRequest cubeMapImageRequest(device.getLogicalDevice(), device.getPhysicalDevice());
		cubeMapImageRequest.format = vk::Format::eR16G16B16A16Sfloat;
		cubeMapImageRequest.layers = 6;
		cubeMapImageRequest.width = CUBE_MAP_SIZE;
		cubeMapImageRequest.height = CUBE_MAP_SIZE;
		cubeMapImageRequest.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled |
			vk::ImageUsageFlagBits::eColorAttachment;
		cubeMapImageRequest.imageFlags = vk::ImageCreateFlagBits::eCubeCompatible;
		core::Utilities::createImage(cubeMapImageRequest, imageIrradianceCube.image,
			imageIrradianceCube.imageMemory);

		core::ImageViewInfoRequest cubeMapImageViewRequest(device.getLogicalDevice(), imageIrradianceCube.image);
		cubeMapImageViewRequest.format = vk::Format::eR16G16B16A16Sfloat;
		cubeMapImageViewRequest.layerCount = 6;
		cubeMapImageViewRequest.imageType = vk::ImageViewType::eCube;
		core::Utilities::createImageView(cubeMapImageViewRequest, imageIrradianceCube.imageView);

		vk::SamplerCreateInfo samplerInfo;
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.anisotropyEnable = VK_TRUE;
		samplerInfo.maxAnisotropy = 16;
		samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		imageIrradianceCube.sampler = device.getLogicalDevice().createSampler(samplerInfo);
#pragma endregion ImageAndSamplerCreate
#pragma region RenderPass
		//SET UP RENDER PASS
		vk::AttachmentDescription colorAttachment;
		colorAttachment.format = vk::Format::eR16G16B16A16Sfloat;
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		colorAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::AttachmentReference colorAttachmentRef;
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::SubpassDescription subPass;
		subPass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subPass.colorAttachmentCount = 1;
		subPass.pColorAttachments = &colorAttachmentRef;

		// Use subpass dependencies for layout transitions
		std::array<vk::SubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
			vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
			vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		vk::RenderPassCreateInfo renderPassInfo;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subPass;
		renderPassInfo.dependencyCount = 2;
		renderPassInfo.pDependencies = dependencies.data();

		vk::RenderPass renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
#pragma endregion RenderPass
#pragma region VertexBuffer
		//DEFINE THE VERTEX BUFFER LAYOUT
		vk::VertexInputBindingDescription vertexInputBindingDescription;
		vertexInputBindingDescription.binding = 0;
		vertexInputBindingDescription.stride = sizeof(glm::vec3);
		vertexInputBindingDescription.inputRate = vk::VertexInputRate::eVertex;

		std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes;
		vertexInputAttributes.push_back({ 0, 0, vk::Format::eR32G32B32Sfloat, 0 });

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &vertexInputBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributes.data();

		//DEFINE THE VERTEX BUFFER
		vk::Buffer vertexBuffer;
		vk::DeviceMemory vertexBufferMemory;

		core::BufferInfoRequest vertexCubeVerticesBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
		vertexCubeVerticesBufferRequest.size = sizeof(cubeVertices[0]) * cubeVertices.size();
		vertexCubeVerticesBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
		vertexCubeVerticesBufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent;
		core::Utilities::createBuffer(vertexCubeVerticesBufferRequest, vertexBuffer, vertexBufferMemory);

		void* data;
		if (vk::Result result = device.getLogicalDevice().mapMemory(vertexBufferMemory, 0,
			vertexCubeVerticesBufferRequest.size, {},
			&data); result != vk::Result::eSuccess)
		{
			loggerError("failed to map memory");
		}
		memcpy(data, cubeVertices.data(), vertexCubeVerticesBufferRequest.size);
		device.getLogicalDevice().unmapMemory(vertexBufferMemory);
#pragma endregion VertexBuffer
#pragma region UniformBuffer
		//DEFINE THE UNIFORM BUFFER LAYOUT
		vk::DescriptorPool descriptorPool;

		std::vector<vk::DescriptorPoolSize> poolSizes(2);
		poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
		poolSizes[0].descriptorCount = 1;
		poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
		poolSizes[1].descriptorCount = 1;

		vk::DescriptorPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = 1;

		descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);

		std::vector<vk::DescriptorSetLayoutBinding> bindings(2);

		bindings[0].binding = 0;
		bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;
		bindings[0].pImmutableSamplers = nullptr;

		bindings[1].binding = 1;
		bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
		bindings[1].pImmutableSamplers = nullptr;

		vk::DescriptorSetLayoutCreateInfo layoutInfo;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		vk::DescriptorSetLayout descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);

		//DEFINE THE UNIFORM BUFFER AND SAMPLER
		vk::DescriptorSet descriptorSet;

		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &descriptorSetLayout;

		descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

		vk::DescriptorImageInfo hdrImageInfo;
		hdrImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		hdrImageInfo.imageView = hdrTexture->getImageView();
		hdrImageInfo.sampler = hdrTexture->getSampler();

		vk::WriteDescriptorSet hdrDescriptorWrite;
		hdrDescriptorWrite.dstSet = descriptorSet;
		hdrDescriptorWrite.dstBinding = 1;
		hdrDescriptorWrite.dstArrayElement = 0;
		hdrDescriptorWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		hdrDescriptorWrite.descriptorCount = 1;
		hdrDescriptorWrite.pImageInfo = &hdrImageInfo;

		device.getLogicalDevice().updateDescriptorSets(hdrDescriptorWrite, nullptr);

		vk::Buffer uboUniformBuffer;
		vk::DeviceMemory uboUniformBufferMemory;
		core::BufferInfoRequest uboBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
		uboBufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
		uboBufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent;
		uboBufferRequest.size = sizeof(UniformBufferObject);
		core::Utilities::createBuffer(uboBufferRequest, uboUniformBuffer, uboUniformBufferMemory);

		vk::DescriptorBufferInfo uboBufferInfo;
		uboBufferInfo.buffer = uboUniformBuffer;
		uboBufferInfo.offset = 0;
		uboBufferInfo.range = sizeof(UniformBufferObject);

		vk::WriteDescriptorSet uboDescriptorWrite;
		uboDescriptorWrite.dstSet = descriptorSet;
		uboDescriptorWrite.dstBinding = 0;
		uboDescriptorWrite.dstArrayElement = 0;
		uboDescriptorWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
		uboDescriptorWrite.descriptorCount = 1;
		uboDescriptorWrite.pBufferInfo = &uboBufferInfo;

		device.getLogicalDevice().updateDescriptorSets(uboDescriptorWrite, nullptr);

#pragma endregion UniformBuffer
#pragma region GraphicsPipline
		//DEFINE GRAPHICS PIPELINE
		vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		vk::Viewport viewport;
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(CUBE_MAP_SIZE);
		viewport.height = static_cast<float>(CUBE_MAP_SIZE);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		vk::Rect2D scissor;
		scissor.offset = vk::Offset2D(0, 0);
		scissor.extent = vk::Extent2D(CUBE_MAP_SIZE, CUBE_MAP_SIZE);

		vk::PipelineViewportStateCreateInfo viewportState;
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;

		vk::PipelineRasterizationStateCreateInfo rasterizer;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eBack;
		rasterizer.frontFace = vk::FrontFace::eClockwise;
		rasterizer.depthBiasEnable = VK_FALSE;

		vk::PipelineMultisampleStateCreateInfo multisampling;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment;
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
			| vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		colorBlendAttachment.blendEnable = VK_FALSE;

		vk::PipelineColorBlendStateCreateInfo colorBlending;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
		vk::PipelineLayout pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

		vk::GraphicsPipelineCreateInfo pipelineInfo;
		pipelineInfo.stageCount = static_cast<uint32_t>(shaderIrradianceCube->getShaderStages().size());
		pipelineInfo.pStages = shaderIrradianceCube->getShaderStages().data();
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;

		vk::Pipeline graphicsPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;
#pragma endregion GraphicsPipline
#pragma region ImageHelper
		OffScreenHelper imageHelper;

		core::ImageInfoRequest imageRequest(device.getLogicalDevice(), device.getPhysicalDevice());
		imageRequest.format = vk::Format::eR16G16B16A16Sfloat;
		imageRequest.width = CUBE_MAP_SIZE;
		imageRequest.height = CUBE_MAP_SIZE;
		imageRequest.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
		core::Utilities::createImage(imageRequest, imageHelper.image, imageHelper.memory);

		core::ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), imageHelper.image);
		imageViewRequest.format = vk::Format::eR16G16B16A16Sfloat;
		core::Utilities::createImageView(imageViewRequest, imageHelper.view);

		//Create frame buffers for each face.
		vk::FramebufferCreateInfo framebufferInfo{};
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &imageHelper.view;
		framebufferInfo.width = CUBE_MAP_SIZE;
		framebufferInfo.height = CUBE_MAP_SIZE;
		framebufferInfo.layers = 1;

		imageHelper.framebuffer = device.getLogicalDevice().createFramebuffer(framebufferInfo);
#pragma endregion ImageHelper
#pragma region ImageTransition
		//transition helper image layout
		vk::UniqueCommandBuffer commandBufferInitHelperImageTransition = core::Utilities::beginSingleTimeCommands(
			device.getLogicalDevice(), commandPool.get());

		core::Utilities::transitionImageLayout(commandBufferInitHelperImageTransition.get(), imageHelper.image,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageAspectFlagBits::eColor);

		core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandBufferInitHelperImageTransition);

		//transition cube image layout
		vk::UniqueCommandBuffer commandBufferInitCubeImage = core::Utilities::beginSingleTimeCommands(
			device.getLogicalDevice(), commandPool.get());

		core::Utilities::transitionImageLayout(commandBufferInitCubeImage.get(), imageIrradianceCube.image,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageAspectFlagBits::eColor, 6);

		core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandBufferInitCubeImage);
#pragma endregion ImageTransition
#pragma region Draw
		vk::UniqueCommandBuffer commandBufferDraw = core::Utilities::beginSingleTimeCommands(
			device.getLogicalDevice(), commandPool.get());

		vk::ClearValue clearColor{ std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f} };

		vk::RenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.framebuffer = imageHelper.framebuffer;
		renderPassBeginInfo.renderArea = vk::Rect2D({ 0, 0 }, { CUBE_MAP_SIZE, CUBE_MAP_SIZE });
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = &clearColor;

		//DRAW COMMAND 
		for (uint32_t face = 0; face < 6; ++face)
		{
			updateUniformBuffer(CameraViewMatrix::captureViews[face], CameraViewMatrix::captureProjection,
				uboUniformBufferMemory);

			// Begin render pass and render to the specific cube face
			commandBufferDraw.get().beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

			// Bind pipeline, descriptor sets, and draw commands
			commandBufferDraw.get().bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
			commandBufferDraw.get().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0,
				descriptorSet,
				{});

			// Bind vertex buffer
			vk::DeviceSize offsets[] = { 0 };
			commandBufferDraw.get().bindVertexBuffers(0, vertexBuffer, offsets);
			commandBufferDraw.get().draw(static_cast<uint32_t>(cubeVertices.size()), 1, 0, 0);

			commandBufferDraw.get().endRenderPass();

			// Ensure synchronization between rendering and copying by transitioning the image layout
			core::Utilities::transitionImageLayout(commandBufferDraw.get(), imageHelper.image,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageLayout::eTransferSrcOptimal,
				vk::ImageAspectFlagBits::eColor);

			// Set up the copy region for the transfer operation
			vk::ImageCopy copyRegion = {};
			copyRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			copyRegion.srcSubresource.baseArrayLayer = 0;
			copyRegion.srcSubresource.mipLevel = 0;
			copyRegion.srcSubresource.layerCount = 1;
			copyRegion.srcOffset = vk::Offset3D{ 0, 0, 0 };

			copyRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			copyRegion.dstSubresource.baseArrayLayer = face;
			copyRegion.dstSubresource.mipLevel = 0; // Set mip level to 0 for each face
			copyRegion.dstSubresource.layerCount = 1;
			copyRegion.dstOffset = vk::Offset3D{ 0, 0, 0 };

			copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
			copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
			copyRegion.extent.depth = 1;

			// Copy the image from the framebuffer to the cube map face
			commandBufferDraw.get().copyImage(imageHelper.image, vk::ImageLayout::eTransferSrcOptimal,
				imageIrradianceCube.image,
				vk::ImageLayout::eTransferDstOptimal, 1, &copyRegion);

			// Transition the image back to color attachment layout for the next face
			core::Utilities::transitionImageLayout(commandBufferDraw.get(), imageHelper.image,
				vk::ImageLayout::eTransferSrcOptimal,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageAspectFlagBits::eColor);
		}

		// Create a fence to wait for completion
		vk::Fence renderFence = device.getLogicalDevice().createFence({});
		core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandBufferDraw, renderFence);

		// Wait for the command buffer to finish executing
		if (vk::Result result = device.getLogicalDevice().waitForFences(renderFence, VK_TRUE, UINT64_MAX); result !=
			vk::Result::eSuccess)
		{
			loggerError("Failed to to wait for Fence IBL:");
		}
#pragma endregion Draw
#pragma region EndImageTransition
		//transition cube image layout to the finial layout
		vk::UniqueCommandBuffer commandBufferEndTransition = core::Utilities::beginSingleTimeCommands(
			device.getLogicalDevice(), commandPool.get());

		core::Utilities::transitionImageLayout(commandBufferEndTransition.get(), imageIrradianceCube.image,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::ImageAspectFlagBits::eColor, 6);

		core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandBufferEndTransition);
#pragma endregion EndImageTransition
#pragma region CleanUp
		//cleanUp
		device.getLogicalDevice().destroyFence(renderFence);

		device.getLogicalDevice().destroyBuffer(vertexBuffer);
		device.getLogicalDevice().freeMemory(vertexBufferMemory);

		device.getLogicalDevice().destroyBuffer(uboUniformBuffer);
		device.getLogicalDevice().freeMemory(uboUniformBufferMemory);

		device.getLogicalDevice().destroyFramebuffer(imageHelper.framebuffer);
		device.getLogicalDevice().freeMemory(imageHelper.memory);
		device.getLogicalDevice().destroyImageView(imageHelper.view);
		device.getLogicalDevice().destroyImage(imageHelper.image);

		device.getLogicalDevice().destroyRenderPass(renderPass);
		device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
		device.getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);
		device.getLogicalDevice().destroyPipeline(graphicsPipeline);
		device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
#pragma endregion CleanUp
	}

	void IBL::generateBRDFLUT()
	{
#pragma region ImageCreate
		// BRDF LUT image setup
		core::ImageInfoRequest brdfLUTImageRequest(device.getLogicalDevice(), device.getPhysicalDevice());
		brdfLUTImageRequest.format = vk::Format::eR16G16Sfloat;
		brdfLUTImageRequest.width = CUBE_MAP_SIZE;
		brdfLUTImageRequest.height = CUBE_MAP_SIZE;
		brdfLUTImageRequest.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
		core::Utilities::createImage(brdfLUTImageRequest, brdfLUTImage.image, brdfLUTImage.imageMemory);

		// BRDF LUT image view
		core::ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), brdfLUTImage.image);
		imageViewRequest.format = vk::Format::eR16G16Sfloat;
		core::Utilities::createImageView(imageViewRequest, brdfLUTImage.imageView);

		vk::SamplerCreateInfo samplerInfo;
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.anisotropyEnable = VK_TRUE;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 1.0f;
		samplerInfo.maxAnisotropy = 1.0;
		samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		brdfLUTImage.sampler = device.getLogicalDevice().createSampler(samplerInfo);
#pragma endregion ImageCreate
#pragma region RenderPass
		//SET UP RENDER PASS
		vk::AttachmentDescription colorAttachment;
		colorAttachment.format = vk::Format::eR16G16Sfloat;
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		colorAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::AttachmentReference colorAttachmentRef;
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::SubpassDescription subPass;
		subPass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subPass.colorAttachmentCount = 1;
		subPass.pColorAttachments = &colorAttachmentRef;

		vk::SubpassDependency dependency;
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = vk::PipelineStageFlagBits::eTransfer;
		dependency.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

		vk::RenderPassCreateInfo renderPassInfo;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subPass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		vk::RenderPass renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
#pragma endregion RenderPass
#pragma region VertexBuffer
		//DEFINE THE VERTEX BUFFER
		vk::VertexInputBindingDescription vertexInputBindingDescription;
		vertexInputBindingDescription.binding = 0;
		vertexInputBindingDescription.stride = sizeof(QuadVertex);
		vertexInputBindingDescription.inputRate = vk::VertexInputRate::eVertex;

		std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes;
		vertexInputAttributes.push_back({ 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(QuadVertex, position) });
		vertexInputAttributes.push_back({ 1, 0, vk::Format::eR32G32Sfloat, offsetof(QuadVertex, texture) });

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &vertexInputBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributes.data();

		vk::Buffer quadVertexBuffer;
		vk::DeviceMemory quadVertexBufferMemory;

		core::BufferInfoRequest quadBufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
		quadBufferInfo.size = sizeof(quad[0]) * quad.size();
		quadBufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer;
		quadBufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent;
		core::Utilities::createBuffer(quadBufferInfo, quadVertexBuffer, quadVertexBufferMemory);

		void* data;
		if (vk::Result result = device.getLogicalDevice().mapMemory(quadVertexBufferMemory, 0, quadBufferInfo.size, {},
			&data); result != vk::Result::eSuccess)
		{
			loggerError("failed to map memory");
		}
		memcpy(data, quad.data(), quadBufferInfo.size);
		device.getLogicalDevice().unmapMemory(quadVertexBufferMemory);
#pragma endregion VertexBuffer
#pragma region GraphicsPipline
		//DEFINE GRAPHICS PIPELINE
		vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		vk::Viewport viewport;
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(CUBE_MAP_SIZE);
		viewport.height = static_cast<float>(CUBE_MAP_SIZE);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		vk::Rect2D scissor;
		scissor.offset = vk::Offset2D(0, 0);
		scissor.extent = vk::Extent2D(CUBE_MAP_SIZE, CUBE_MAP_SIZE);

		vk::PipelineViewportStateCreateInfo viewportState;
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;

		vk::PipelineRasterizationStateCreateInfo rasterizer;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eBack;
		rasterizer.frontFace = vk::FrontFace::eClockwise;
		rasterizer.depthBiasEnable = VK_FALSE;

		vk::PipelineMultisampleStateCreateInfo multisampling;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment;
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
			| vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		colorBlendAttachment.blendEnable = VK_FALSE;

		vk::PipelineColorBlendStateCreateInfo colorBlending;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
		vk::PipelineLayout pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

		vk::GraphicsPipelineCreateInfo pipelineInfo;
		pipelineInfo.stageCount = static_cast<uint32_t>(brdfLUTShader->getShaderStages().size());
		pipelineInfo.pStages = brdfLUTShader->getShaderStages().data();
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;

		vk::Pipeline graphicsPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;
#pragma endregion GraphicsPipline
#pragma region FrameBuffer
		// Framebuffer for BRDF LUT rendering
		vk::FramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &brdfLUTImage.imageView;
		framebufferInfo.width = CUBE_MAP_SIZE;
		framebufferInfo.height = CUBE_MAP_SIZE;
		framebufferInfo.layers = 1;

		vk::Framebuffer framebuffer = device.getLogicalDevice().createFramebuffer(framebufferInfo);
#pragma endregion FrameBuffer
#pragma region Draw
		// Command buffer recording
		vk::UniqueCommandBuffer commandBuffer = core::Utilities::beginSingleTimeCommands(
			device.getLogicalDevice(), commandPool.get());

		vk::ClearValue clearColor{ std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f} };
		vk::RenderPassBeginInfo renderPassBeginInfo = {};
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.framebuffer = framebuffer;
		renderPassBeginInfo.renderArea = vk::Rect2D({ 0, 0 }, { CUBE_MAP_SIZE, CUBE_MAP_SIZE });
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = &clearColor;

		commandBuffer->beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
		commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

		vk::DeviceSize offsets[] = { 0 };
		commandBuffer->bindVertexBuffers(0, quadVertexBuffer, offsets);
		commandBuffer->draw(6, 1, 0, 0);
		commandBuffer->endRenderPass();

		// Execute and wait
		vk::Fence renderFence = device.getLogicalDevice().createFence({});
		core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), commandBuffer, renderFence);
		if (vk::Result result = device.getLogicalDevice().waitForFences(renderFence, VK_TRUE, UINT64_MAX); result !=
			vk::Result::eSuccess)
		{
			loggerError("Failed to to wait for Fence BRDFLUT:");
		}
#pragma endregion Draw
#pragma region EndImageTransition
		// Transition image layout to shader-readable
		vk::UniqueCommandBuffer transitionCommandBuffer = core::Utilities::beginSingleTimeCommands(
			device.getLogicalDevice(), commandPool.get());

		core::Utilities::transitionImageLayout(
			transitionCommandBuffer.get(),
			brdfLUTImage.image,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::ImageAspectFlagBits::eColor
		);
		core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), transitionCommandBuffer);
#pragma endregion EndImageTransition
#pragma region CleanUp
		//cleanUp
		device.getLogicalDevice().destroyFramebuffer(framebuffer);
		device.getLogicalDevice().destroyFence(renderFence);

		device.getLogicalDevice().destroyBuffer(quadVertexBuffer);
		device.getLogicalDevice().freeMemory(quadVertexBufferMemory);

		device.getLogicalDevice().destroyRenderPass(renderPass);
		device.getLogicalDevice().destroyPipeline(graphicsPipeline);
		device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
#pragma endregion CleanUp
	}

	void IBL::updateUniformBuffer(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix,
		const vk::DeviceMemory& uniformBufferMemory) const
	{
		UniformBufferObject ubo;
		ubo.view = viewMatrix;
		ubo.projection = projectionMatrix;

		void* data;
		vk::Result result = device.getLogicalDevice().mapMemory(uniformBufferMemory, 0, sizeof(ubo), {}, &data);
		if (result == vk::Result::eSuccess)
		{
			memcpy(data, &ubo, sizeof(ubo));
			device.getLogicalDevice().unmapMemory(uniformBufferMemory);
		}
	}

	void IBL::cleanUpImage(const ImageData& image) const
	{
		device.getLogicalDevice().destroyImage(image.image);
		device.getLogicalDevice().destroyImageView(image.imageView);
		device.getLogicalDevice().destroySampler(image.sampler);
		device.getLogicalDevice().freeMemory(image.imageMemory);
	}
}
