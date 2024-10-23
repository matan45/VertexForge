#include "OffScreenViewPort.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/CommandPool.hpp"
#include "../core/Utilities.hpp"
#include "../core/RenderManager.hpp"
#include "../render/RenderPassHandler.hpp"
#include "print/Logger.hpp"

#include <imgui_impl_vulkan.h>


namespace imguiPass {
	OffScreenViewPort::OffScreenViewPort(core::Device& device, core::SwapChain& swapChain) :device{ device }
		, swapChain{ swapChain }
	{
		commandPool = new core::CommandPool(device, swapChain);
	}

	OffScreenViewPort::~OffScreenViewPort()
	{
		delete commandPool;
		delete renderPassHandler;  // Properly clean up the render pass handler
	}

	void OffScreenViewPort::init()
	{
		createSampler();
		createOffscreenResources();

		renderPassHandler = new render::RenderPassHandler(device, swapChain, offscreenResources);
		renderPassHandler->init();
	}

	vk::DescriptorSet OffScreenViewPort::render()
	{

		// Get the command buffer for this frame
		vk::CommandBuffer commandBuffer = commandPool->getCommandBuffer(core::RenderManager::getImageIndex());

		// Reset the command buffer for reuse
		commandBuffer.reset();

		// Begin recording commands for the acquired image
		commandBuffer.begin(vk::CommandBufferBeginInfo{});

		// Begin the render pass (record drawing commands here)
		draw(commandBuffer);

		// End command buffer recording
		commandBuffer.end();

		// Submit the command buffer to the compute queue
		vk::SubmitInfo submitInfo(
			0, nullptr, nullptr,
			1, &commandBuffer,
			0, nullptr
		);
		device.getGraphicsQueue().submit(submitInfo, nullptr);
		device.getGraphicsQueue().waitIdle();

		// Return the descriptor set for ImGui rendering
		return offscreenResources[core::RenderManager::getImageIndex()].descriptorSet;
	}

	void OffScreenViewPort::cleanUp() const
	{
		device.getLogicalDevice().waitIdle();

		renderPassHandler->cleanUp();

		commandPool->cleanUp();

		device.getLogicalDevice().destroySampler(sampler);

		for (auto const& resources : offscreenResources) {
			device.getLogicalDevice().destroyImageView(resources.colorImageView);
			device.getLogicalDevice().destroyImageView(resources.depthImageView);
			device.getLogicalDevice().destroyImage(resources.colorImage);
			device.getLogicalDevice().destroyImage(resources.depthImage);
			device.getLogicalDevice().freeMemory(resources.colorImageMemory);
			device.getLogicalDevice().freeMemory(resources.depthImageMemory);
		}
	}

	void OffScreenViewPort::draw(const vk::CommandBuffer& commandBuffer) const
	{
		renderPassHandler->draw(commandBuffer, core::RenderManager::getImageIndex());
	}

	void OffScreenViewPort::createOffscreenResources()
	{
		vk::Format colorFormat = swapChain.getSwapchainImageFormat();
		vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();

		core::ImageInfoRequest imageColorInfo(device.getLogicalDevice(), device.getPhysicalDevice());
		imageColorInfo.width = swapChain.getSwapchainExtent().width;
		imageColorInfo.height = swapChain.getSwapchainExtent().height;
		imageColorInfo.format = colorFormat;
		imageColorInfo.tiling = vk::ImageTiling::eOptimal;
		imageColorInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
		imageColorInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
		
		core::ImageInfoRequest imageDepthInfo(device.getLogicalDevice(), device.getPhysicalDevice());
		imageDepthInfo.width = swapChain.getSwapchainExtent().width;
		imageDepthInfo.height = swapChain.getSwapchainExtent().height;
		imageDepthInfo.format = depthFormat;
		imageDepthInfo.tiling = vk::ImageTiling::eOptimal;
		imageDepthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
		imageDepthInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

		for (size_t i = 0; i < swapChain.getImageCount(); i++) {
			OffscreenResources resources;

			// Create color image for off-screen rendering
			core::Utilities::createImage(imageColorInfo, resources.colorImage, resources.colorImageMemory);
			core::Utilities::createImageView(device.getLogicalDevice(), resources.colorImage, colorFormat, vk::ImageAspectFlagBits::eColor, resources.colorImageView);

			// Create depth image
			core::Utilities::createImage(imageDepthInfo, resources.depthImage, resources.depthImageMemory);
			core::Utilities::createImageView(device.getLogicalDevice(), resources.depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, resources.depthImageView);


			updateDescriptorSets(resources.descriptorSet, resources.colorImageView);

			// Store resources
			offscreenResources.push_back(std::move(resources));
		}
	}

	void OffScreenViewPort::updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const
	{
		descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	void OffScreenViewPort::createSampler()
	{
		vk::SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;

		samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

		vk::PhysicalDeviceProperties properties = device.getPhysicalDevice().getProperties();

		// Retrieve the maximum anisotropy level supported by the device
		float maxAnisotropy = properties.limits.maxSamplerAnisotropy;

		// Mipmapping options
		samplerInfo.anisotropyEnable = VK_TRUE;  // Enable anisotropic filtering if supported
		samplerInfo.maxAnisotropy = maxAnisotropy;
		samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;  // Use black for border sampling
		samplerInfo.unnormalizedCoordinates = VK_FALSE;  // Use normalized coordinates [0, 1]
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = vk::CompareOp::eAlways;

		// Create the sampler
		sampler = device.getLogicalDevice().createSampler(samplerInfo);
	}

}
