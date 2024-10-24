#include "TriangleRenderer.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/Shader.hpp"
#include "../core/Utilities.hpp"
#include "print/Logger.hpp"

namespace render {
	TriangleRenderer::TriangleRenderer(core::Device& device, core::SwapChain& swapChain, std::vector<imguiPass::OffscreenResources>& offscreenResources) : device{ device },
		swapChain{ swapChain }, offscreenResources{ offscreenResources }
	{
	}

	void TriangleRenderer::init()
	{
		triangle = new core::Shader(device);

		triangle->readShader("../../resources/shaders/Triangle.glsl");

		bindingDescription();
		attributeDescriptions();
		createVertexBuffer();
		createRenderPass();
		createFramebuffers();
		createGraphicsPipeline();
	}

	void TriangleRenderer::recreate()
	{
		for (auto framebuffer : framebuffers) {
			device.getLogicalDevice().destroyFramebuffer(framebuffer);
		}

		// Destroy the graphics pipeline, pipeline layout, and render pass
		device.getLogicalDevice().destroyPipeline(graphicsPipeline);
		device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
		device.getLogicalDevice().destroyRenderPass(renderPass);

		createRenderPass();
		createFramebuffers();
		createGraphicsPipeline();
	}

	void TriangleRenderer::cleanUp() const
	{
		// Cleanup shader modules
		triangle->cleanUp();

		for (auto framebuffer : framebuffers) {
			device.getLogicalDevice().destroyFramebuffer(framebuffer);
		}

		// Destroy the graphics pipeline, pipeline layout, and render pass
		device.getLogicalDevice().destroyPipeline(graphicsPipeline);
		device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
		device.getLogicalDevice().destroyRenderPass(renderPass);

		// Destroy vertex buffer and memory
		device.getLogicalDevice().destroyBuffer(vertexBuffer);
		device.getLogicalDevice().freeMemory(vertexBufferMemory);

		delete triangle;
	}

	void TriangleRenderer::recordCommandBuffer(vk::CommandBuffer commandBuffer, uint32_t imageIndex) const
	{
		vk::RenderPassBeginInfo renderPassInfo{};
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = framebuffers[imageIndex];
		renderPassInfo.renderArea.offset.x = 0;
		renderPassInfo.renderArea.offset.y = 0;
		renderPassInfo.renderArea.extent = swapChain.getSwapchainExtent();

		std::array<vk::ClearValue, 1> clearValues{};
		clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});  // Sky color
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		// Begin render pass
		commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

		// Bind the graphics pipeline
		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

		// Bind the vertex buffer
		std::array<vk::DeviceSize, 1> offsets = { 0 };
		commandBuffer.bindVertexBuffers(0, vertexBuffer, offsets);

		// Draw the triangle
		commandBuffer.draw(3, 1, 0, 0);

		// End render pass
		commandBuffer.endRenderPass();
	}

	void TriangleRenderer::createVertexBuffer()
	{
		std::vector<Vertex> vertices = {
		   {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},  // Vertex 1: position, color
		   {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},   // Vertex 2
		   {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}   // Vertex 3
		};

		core::BufferInfoRequest bufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
		bufferInfo.size = sizeof(vertices[0]) * vertices.size();
		bufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer;
		bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
		core::Utilities::createBuffer(bufferInfo, vertexBuffer, vertexBufferMemory);

		void* data;
		vk::Result result = device.getLogicalDevice().mapMemory(vertexBufferMemory, 0, bufferInfo.size, {}, &data);
		if (result != vk::Result::eSuccess) {
			loggerError("failed to map memory");
		}
		memcpy(data, vertices.data(), bufferInfo.size);
		device.getLogicalDevice().unmapMemory(vertexBufferMemory);
	}

	void TriangleRenderer::createGraphicsPipeline()
	{
		vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &vertexInputBindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributes.data();

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		vk::Viewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(swapChain.getSwapchainExtent().width);
		viewport.height = static_cast<float>(swapChain.getSwapchainExtent().height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		vk::Rect2D scissor{};
		scissor.offset.x = 0;
		scissor.offset.y = 0;
		scissor.extent = swapChain.getSwapchainExtent();

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
		rasterizer.cullMode = vk::CullModeFlagBits::eBack;
		rasterizer.frontFace = vk::FrontFace::eClockwise;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA;
		colorBlendAttachment.blendEnable = VK_FALSE;

		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.stageCount = static_cast<uint32_t>(triangle->getShaderStages().size());
		pipelineInfo.pStages = triangle->getShaderStages().data();
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;

		graphicsPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;

	}

	void TriangleRenderer::createFramebuffers()
	{
		framebuffers.resize(offscreenResources.size());

		for (uint32_t i = 0; i < framebuffers.size(); i++) {
			vk::ImageView viewImage = offscreenResources[i].colorImageView;

			vk::FramebufferCreateInfo framebufferInfo{};
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = &viewImage;
			framebufferInfo.width = swapChain.getSwapchainExtent().width;
			framebufferInfo.height = swapChain.getSwapchainExtent().height;
			framebufferInfo.layers = 1;

			framebuffers[i] = device.getLogicalDevice().createFramebuffer(framebufferInfo);
		}
	}

	void TriangleRenderer::createRenderPass()
	{
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

		renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
	}

	void TriangleRenderer::bindingDescription()
	{
		vertexInputBindingDescription.binding = 0;
		vertexInputBindingDescription.stride = sizeof(Vertex);
		vertexInputBindingDescription.inputRate = vk::VertexInputRate::eVertex;
	}

	void TriangleRenderer::attributeDescriptions()
	{
		vertexInputAttributes.push_back({ 0,0,vk::Format::eR32G32Sfloat, offsetof(Vertex, position) });
		vertexInputAttributes.push_back({ 1,0,vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color) });
	}

}
