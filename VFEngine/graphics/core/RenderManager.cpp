#include "RenderManager.hpp"
#include "Device.hpp"
#include "SwapChain.hpp"
#include "CommandPool.hpp"
#include "DeferredDeletionQueue.hpp"
#include "print/Log.hpp"
#include "../window/Window.hpp"
#include "../imguiPass/ImguiRender.hpp"

namespace core {


	RenderManager::RenderManager(Device& device, SwapChain& swapChain, const window::Window* window, bool imguiEnabled)
		: device{ device }, swapChain{ swapChain }, window{ window }, imguiEnabled{ imguiEnabled }
	{

	}

	RenderManager::~RenderManager()
	{
		globalDeletionQueue = nullptr;
	}

	void RenderManager::init()
	{
		commandPool = std::make_unique<CommandPool>(device, swapChain);

		if (imguiEnabled)
		{
			imguiRender = std::make_unique<imguiPass::ImguiRender>(device, swapChain, *commandPool, window);
			imguiRender->init();
		}
		else
		{
			createPresentPass();
			createPresentFrameBuffers();
		}

		deletionQueue = std::make_unique<DeferredDeletionQueue>(device);
		globalDeletionQueue = deletionQueue.get();

		uint32_t imageCount = swapChain.getImageCount();

		// Per-frame sync objects
		imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

		// Per-swapchain-image semaphores (as suggested by validation layer)
		renderFinishedSemaphores.resize(imageCount);

		// Track which fence each image is using (initially null)
		imagesInFlight.resize(imageCount, nullptr);

		vk::SemaphoreCreateInfo semaphoreInfo{};
		vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			imageAvailableSemaphores[i] = device.getLogicalDevice().createSemaphore(semaphoreInfo);
			inFlightFences[i] = device.getLogicalDevice().createFence(fenceInfo);
		}

		for (uint32_t i = 0; i < imageCount; i++) {
			renderFinishedSemaphores[i] = device.getLogicalDevice().createSemaphore(semaphoreInfo);
		}
	}

	void RenderManager::render()
	{
		vk::Result result = device.getLogicalDevice().waitForFences(
			1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
		if (result != vk::Result::eSuccess) {
			vfLogError("failed to wait for in-flight fence");
		}

		result = device.getLogicalDevice().acquireNextImageKHR(
			swapChain.getSwapchain(),
			UINT64_MAX,
			imageAvailableSemaphores[currentFrame],
			nullptr,
			&imageIndex
		);

		if (result == vk::Result::eErrorOutOfDateKHR) {
			recreate(window->getHeight(), window->getWidth());
			return;
		}

		// Check if a previous frame is using this image (wait for it)
		if (imagesInFlight[imageIndex]) {
			result = device.getLogicalDevice().waitForFences(
				1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
			if (result != vk::Result::eSuccess) {
				vfLogError("failed to wait for image in flight fence");
			}
		}
		// Mark this image as now being in use by this frame
		imagesInFlight[imageIndex] = inFlightFences[currentFrame];

		// Reset the fence only after we know we will submit work
		result = device.getLogicalDevice().resetFences(1, &inFlightFences[currentFrame]);
		if (result != vk::Result::eSuccess) {
			vfLogError("failed to reset fence");
		}

		commandPool->resetCommandBuffer(imageIndex);

		vk::CommandBuffer commandBuffer = commandPool->getCommandBuffer(imageIndex);

		// Begin recording commands for the acquired image
		commandBuffer.begin(vk::CommandBufferBeginInfo{});

		// Render the scene using the command buffer for this swapchain image
		draw(commandBuffer);

		// End command buffer recording
		commandPool->getCommandBuffer(imageIndex).end();

		// Submit the command buffer for rendering
		// Use per-frame semaphore for wait, per-image semaphore for signal
		vk::SubmitInfo submitInfo{};
		std::array<vk::Semaphore, 1> waitSemaphores = { imageAvailableSemaphores[currentFrame] };
		std::array<vk::Semaphore, 1> signalSemaphores = { renderFinishedSemaphores[imageIndex] };
		std::array<vk::PipelineStageFlags, 1> waitStages = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

		vk::CommandBuffer cmdBuffer = commandPool->getCommandBuffer(imageIndex);
		submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
		submitInfo.pWaitSemaphores = waitSemaphores.data();
		submitInfo.pWaitDstStageMask = waitStages.data();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdBuffer;
		submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
		submitInfo.pSignalSemaphores = signalSemaphores.data();

		device.getGraphicsQueue().submit(submitInfo, inFlightFences[currentFrame]);

		// Present the rendered image (use per-image semaphore)
		present(imageIndex);

		// Process deferred deletions for resources that are now safe to destroy
		if (deletionQueue)
		{
			deletionQueue->processDeletions(currentFrame);
		}

		// Advance to next frame
		currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	void RenderManager::recreate(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0) return;  // Skip if minimized

		// Wait for all GPU work to complete before recreating resources
		device.getLogicalDevice().waitIdle();

		commandPool->recreate();  // Reallocate command buffers if needed

		if (imguiEnabled)
		{
			imguiRender->recreate();
		}
		else
		{
			for (auto fb : presentFrameBuffers)
			{
				device.getLogicalDevice().destroyFramebuffer(fb);
			}
			device.getLogicalDevice().destroyRenderPass(presentRenderPass);
			createPresentPass();
			createPresentFrameBuffers();
		}

		// Notify listeners (e.g., OffScreenViewPort for Hi-Z recreation)
		if (onResizeCallback) {
			onResizeCallback();
		}
	}

	void RenderManager::cleanUp() const
	{
		device.getLogicalDevice().waitIdle();

		// Flush any remaining deferred deletions
		if (deletionQueue)
		{
			deletionQueue->flush();
		}

		commandPool->cleanUp();

		if (imguiEnabled)
		{
			imguiRender->cleanUp();
		}
		else
		{
			cleanUpPresentPass();
		}

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			device.getLogicalDevice().destroySemaphore(imageAvailableSemaphores[i]);
			device.getLogicalDevice().destroyFence(inFlightFences[i]);
		}

		for (const auto& semaphore : renderFinishedSemaphores) {
			device.getLogicalDevice().destroySemaphore(semaphore);
		}
	}

	void RenderManager::draw(const vk::CommandBuffer& commandBuffer) const
	{
		if (imguiEnabled)
		{
			imguiRender->render(commandBuffer, imageIndex);
		}
		else if (blitSourceProvider)
		{
			// Blit offscreen color image to swapchain image
			vk::Image srcImage = blitSourceProvider(imageIndex);
			vk::Image dstImage = swapChain.getSwapchainImage(imageIndex);
			auto extent = swapChain.getSwapchainExtent();

			// Transition offscreen image: ShaderReadOnly -> TransferSrc
			vk::ImageMemoryBarrier srcBarrier{};
			srcBarrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			srcBarrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
			srcBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
			srcBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
			srcBarrier.image = srcImage;
			srcBarrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

			// Transition swapchain image: Undefined -> TransferDst
			vk::ImageMemoryBarrier dstBarrier{};
			dstBarrier.oldLayout = vk::ImageLayout::eUndefined;
			dstBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
			dstBarrier.srcAccessMask = {};
			dstBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
			dstBarrier.image = dstImage;
			dstBarrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

			std::array<vk::ImageMemoryBarrier, 2> toTransferBarriers = { srcBarrier, dstBarrier };
			commandBuffer.pipelineBarrier(
				vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eColorAttachmentOutput,
				vk::PipelineStageFlagBits::eTransfer,
				{}, {}, {}, toTransferBarriers);

			// Blit
			vk::ImageBlit blitRegion{};
			blitRegion.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
			blitRegion.srcOffsets[0] = vk::Offset3D{ 0, 0, 0 };
			blitRegion.srcOffsets[1] = vk::Offset3D{ static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1 };
			blitRegion.dstSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
			blitRegion.dstOffsets[0] = vk::Offset3D{ 0, 0, 0 };
			blitRegion.dstOffsets[1] = vk::Offset3D{ static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1 };

			commandBuffer.blitImage(
				srcImage, vk::ImageLayout::eTransferSrcOptimal,
				dstImage, vk::ImageLayout::eTransferDstOptimal,
				1, &blitRegion, vk::Filter::eLinear);

			// Transition offscreen image back: TransferSrc -> ShaderReadOnly
			srcBarrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
			srcBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			srcBarrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
			srcBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

			// Transition swapchain image: TransferDst -> PresentSrc
			dstBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
			dstBarrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
			dstBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			dstBarrier.dstAccessMask = {};

			std::array<vk::ImageMemoryBarrier, 2> toPresentBarriers = { srcBarrier, dstBarrier };
			commandBuffer.pipelineBarrier(
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eBottomOfPipe,
				{}, {}, {}, toPresentBarriers);
		}
		else
		{
			// Fallback: clear swapchain image to magenta (debug: blit source not set)
			vk::ClearValue clearColor = { std::array<float, 4>{1.0f, 0.0f, 1.0f, 1.0f} };

			vk::RenderPassBeginInfo renderPassInfo{};
			renderPassInfo.renderPass = presentRenderPass;
			renderPassInfo.framebuffer = presentFrameBuffers[imageIndex];
			renderPassInfo.renderArea.extent = swapChain.getSwapchainExtent();
			renderPassInfo.clearValueCount = 1;
			renderPassInfo.pClearValues = &clearColor;

			commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
			commandBuffer.endRenderPass();
		}
	}

	void RenderManager::createPresentPass()
	{
		vk::AttachmentDescription colorAttachment{};
		colorAttachment.format = swapChain.getSwapchainImageFormat();
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

		vk::AttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::SubpassDescription subpass{};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		vk::RenderPassCreateInfo renderPassCreateInfo{};
		renderPassCreateInfo.attachmentCount = 1;
		renderPassCreateInfo.pAttachments = &colorAttachment;
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subpass;

		presentRenderPass = device.getLogicalDevice().createRenderPass(renderPassCreateInfo);
	}

	void RenderManager::createPresentFrameBuffers()
	{
		presentFrameBuffers.resize(swapChain.getImageCount());

		for (uint32_t i = 0; i < presentFrameBuffers.size(); i++)
		{
			vk::ImageView viewImage = swapChain.getSwapchainImageView(i);

			vk::FramebufferCreateInfo framebufferInfo{};
			framebufferInfo.renderPass = presentRenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = &viewImage;
			framebufferInfo.width = swapChain.getSwapchainExtent().width;
			framebufferInfo.height = swapChain.getSwapchainExtent().height;
			framebufferInfo.layers = 1;

			presentFrameBuffers[i] = device.getLogicalDevice().createFramebuffer(framebufferInfo);
		}
	}

	void RenderManager::cleanUpPresentPass() const
	{
		for (auto fb : presentFrameBuffers)
		{
			device.getLogicalDevice().destroyFramebuffer(fb);
		}
		device.getLogicalDevice().destroyRenderPass(presentRenderPass);
	}

	void RenderManager::present(uint32_t frameIndex)
	{
		// Present the image to the screen
		vk::PresentInfoKHR presentInfo{};
		std::array<vk::Semaphore, 1> waitSemaphores = { renderFinishedSemaphores[frameIndex] };
		presentInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
		presentInfo.pWaitSemaphores = waitSemaphores.data();
		std::array<vk::SwapchainKHR, 1> swapChains = { swapChain.getSwapchain() };
		presentInfo.swapchainCount = static_cast<uint32_t>(swapChains.size());
		presentInfo.pSwapchains = swapChains.data();
		presentInfo.pImageIndices = &imageIndex;

		vk::Result result = device.getPresentQueue().presentKHR(&presentInfo);

		if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
			recreate(window->getHeight(), window->getWidth());
		}
	}

}
