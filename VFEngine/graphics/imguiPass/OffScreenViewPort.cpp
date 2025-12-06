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
		, commandPool{ std::make_unique<core::CommandPool>(device, swapChain) }
	{
	}

	OffScreenViewPort::~OffScreenViewPort() = default;

	void OffScreenViewPort::init()
	{
		createSampler();
		createOffscreenResources();

		renderPassHandler = std::make_unique<render::RenderPassHandler>(device, swapChain, offscreenResources);
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
		return offscreenResources.colorImages[core::RenderManager::getImageIndex()].descriptorSet;
	}

	void OffScreenViewPort::cleanUp() const
	{
		device.getLogicalDevice().waitIdle();

		renderPassHandler->cleanUp();

		commandPool->cleanUp();

		device.getLogicalDevice().destroySampler(sampler);

		for (auto const& resources : offscreenResources.colorImages) {
			device.getLogicalDevice().destroyImageView(resources.colorImageView);
			device.getLogicalDevice().destroyImage(resources.colorImage);
			device.getLogicalDevice().freeMemory(resources.colorImageMemory);
		}
		device.getLogicalDevice().destroyImageView(offscreenResources.depthImage.depthImageView);
		device.getLogicalDevice().destroyImage(offscreenResources.depthImage.depthImage);
		device.getLogicalDevice().freeMemory(offscreenResources.depthImage.depthImageMemory);
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

		core::DepthImage depth;
		// Create depth image
		core::Utilities::createImage(imageDepthInfo, depth.depthImage, depth.depthImageMemory);
		core::ImageViewInfoRequest imageDepthRequest(device.getLogicalDevice(), depth.depthImage);

		imageDepthRequest.format = depthFormat;
		imageDepthRequest.aspectFlags = vk::ImageAspectFlagBits::eDepth;
		core::Utilities::createImageView(imageDepthRequest, depth.depthImageView);

		vk::UniqueCommandBuffer trasitionDepthImage = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool->getCommandPool());
		core::Utilities::transitionImageLayout(trasitionDepthImage.get(), depth.depthImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageAspectFlagBits::eDepth| vk::ImageAspectFlagBits::eStencil);
		core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), trasitionDepthImage);

		offscreenResources.depthImage = std::move(depth);

		offscreenResources.colorImages.reserve(swapChain.getImageCount());

		for (size_t i = 0; i < swapChain.getImageCount(); i++) {
			core::ColorImage color;
			// Create color image for off-screen rendering
			core::Utilities::createImage(imageColorInfo, color.colorImage, color.colorImageMemory);
			core::ImageViewInfoRequest imageColorViewRequest(device.getLogicalDevice(), color.colorImage);
			imageColorViewRequest.format = colorFormat;
			core::Utilities::createImageView(imageColorViewRequest, color.colorImageView);

			vk::UniqueCommandBuffer trasitionColorImage = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool->getCommandPool());
			core::Utilities::transitionImageLayout(trasitionColorImage.get(), color.colorImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);
			core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), trasitionColorImage);

			updateDescriptorSets(color.descriptorSet, color.colorImageView);

			// Store resources
			offscreenResources.colorImages.push_back(std::move(color));
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
