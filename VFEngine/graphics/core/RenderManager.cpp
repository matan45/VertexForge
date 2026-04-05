#include "RenderManager.hpp"
#include "Device.hpp"
#include "VulkanMemoryManager.hpp"
#include "SwapChain.hpp"
#include "CommandPool.hpp"
#include "DeferredDeletionQueue.hpp"
#include "DynamicRenderingHelpers.hpp"
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
		if (window->isWindowMinimized()) return;

		vk::Result result = device.getLogicalDevice().waitForFences(
			1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
		if (result != vk::Result::eSuccess) {
			vfLogError("failed to wait for in-flight fence");
		}

		uint32_t acquiredImageIndex = 0;
		result = device.getLogicalDevice().acquireNextImageKHR(
			swapChain.getSwapchain(),
			UINT64_MAX,
			imageAvailableSemaphores[currentFrame],
			nullptr,
			&acquiredImageIndex
		);

		if (result == vk::Result::eErrorOutOfDateKHR) {
			recreate(window->getHeight(), window->getWidth());
			return;
		}

		imageIndex.store(acquiredImageIndex, std::memory_order_release);

		// Check if a previous frame is using this image (wait for it)
		if (imagesInFlight[acquiredImageIndex]) {
			result = device.getLogicalDevice().waitForFences(
				1, &imagesInFlight[acquiredImageIndex], VK_TRUE, UINT64_MAX);
			if (result != vk::Result::eSuccess) {
				vfLogError("failed to wait for image in flight fence");
			}
		}
		// Mark this image as now being in use by this frame
		imagesInFlight[acquiredImageIndex] = inFlightFences[currentFrame];

		// Reset the fence only after we know we will submit work
		result = device.getLogicalDevice().resetFences(1, &inFlightFences[currentFrame]);
		if (result != vk::Result::eSuccess) {
			vfLogError("failed to reset fence");
		}

		commandPool->resetCommandBuffer(acquiredImageIndex);

		vk::CommandBuffer commandBuffer = commandPool->getCommandBuffer(acquiredImageIndex);

		// Begin recording commands for the acquired image
		commandBuffer.begin(vk::CommandBufferBeginInfo{});

		// Render the scene using the command buffer for this swapchain image
		draw(commandBuffer, acquiredImageIndex);

		// End command buffer recording
		commandPool->getCommandBuffer(acquiredImageIndex).end();

		// Submit the command buffer for rendering
		// Use per-frame semaphore for wait, per-image semaphore for signal
		vk::SubmitInfo submitInfo{};
		std::array<vk::Semaphore, 1> waitSemaphores = { imageAvailableSemaphores[currentFrame] };
		std::array<vk::Semaphore, 1> signalSemaphores = { renderFinishedSemaphores[acquiredImageIndex] };
		std::array<vk::PipelineStageFlags, 1> waitStages = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

		vk::CommandBuffer cmdBuffer = commandPool->getCommandBuffer(acquiredImageIndex);
		submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
		submitInfo.pWaitSemaphores = waitSemaphores.data();
		submitInfo.pWaitDstStageMask = waitStages.data();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdBuffer;
		submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
		submitInfo.pSignalSemaphores = signalSemaphores.data();

		device.submitGraphics(submitInfo, inFlightFences[currentFrame]);

		// Present the rendered image (use per-image semaphore)
		present(acquiredImageIndex);

		// Process deferred deletions for resources that are now safe to destroy
		// Use monotonic frame counter (not wrapping currentFrame) so FRAMES_BEFORE_DELETE works correctly
		if (deletionQueue)
		{
			deletionQueue->processDeletions(globalFrameCounter);
		}
		globalFrameCounter++;

		// Reclaim GPU memory blocks that are now completely empty
		device.getMemoryManager().reclaimEmptyBlocks();

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

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			device.getLogicalDevice().destroySemaphore(imageAvailableSemaphores[i]);
			device.getLogicalDevice().destroyFence(inFlightFences[i]);
		}

		for (const auto& semaphore : renderFinishedSemaphores) {
			device.getLogicalDevice().destroySemaphore(semaphore);
		}
	}

	void RenderManager::snapshotImGuiDrawData()
	{
		if (imguiEnabled && imguiRender)
		{
			imguiRender->generateAndSnapshotDrawData();
		}
	}

	void RenderManager::draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIdx)
	{
		if (imguiEnabled)
		{
			if (skipNextImguiRender.exchange(false))
			{
				imguiRender->renderEmpty(commandBuffer, imageIdx);
			}
			else
			{
				imguiRender->render(commandBuffer, imageIdx);
			}
		}
		else if (blitSourceProvider)
		{
			// Blit offscreen color image to swapchain image
			vk::Image srcImage = blitSourceProvider(imageIdx);
			vk::Image dstImage = swapChain.getSwapchainImage(imageIdx);
			auto extent = swapChain.getSwapchainExtent();

			// Transition offscreen image: ShaderReadOnly -> TransferSrc
			vk::ImageMemoryBarrier2 srcBarrier{};
			srcBarrier.srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eColorAttachmentOutput;
			srcBarrier.srcAccessMask = vk::AccessFlagBits2::eShaderRead;
			srcBarrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
			srcBarrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
			srcBarrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			srcBarrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
			srcBarrier.image = srcImage;
			srcBarrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

			// Transition swapchain image: Undefined -> TransferDst
			vk::ImageMemoryBarrier2 dstBarrier{};
			dstBarrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
			dstBarrier.srcAccessMask = {};
			dstBarrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
			dstBarrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
			dstBarrier.oldLayout = vk::ImageLayout::eUndefined;
			dstBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
			dstBarrier.image = dstImage;
			dstBarrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

			std::array<vk::ImageMemoryBarrier2, 2> toTransferBarriers = { srcBarrier, dstBarrier };
			vk::DependencyInfo preBlit{};
			preBlit.imageMemoryBarrierCount = static_cast<uint32_t>(toTransferBarriers.size());
			preBlit.pImageMemoryBarriers = toTransferBarriers.data();
			commandBuffer.pipelineBarrier2KHR(preBlit);

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
			srcBarrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
			srcBarrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
			srcBarrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
			srcBarrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
			srcBarrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
			srcBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

			// Transition swapchain image: TransferDst -> PresentSrc
			dstBarrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
			dstBarrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
			dstBarrier.dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
			dstBarrier.dstAccessMask = {};
			dstBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
			dstBarrier.newLayout = vk::ImageLayout::ePresentSrcKHR;

			std::array<vk::ImageMemoryBarrier2, 2> toPresentBarriers = { srcBarrier, dstBarrier };
			vk::DependencyInfo postBlit{};
			postBlit.imageMemoryBarrierCount = static_cast<uint32_t>(toPresentBarriers.size());
			postBlit.pImageMemoryBarriers = toPresentBarriers.data();
			commandBuffer.pipelineBarrier2KHR(postBlit);
		}
		else
		{
			drawPresentClear(commandBuffer, imageIdx);
		}
	}

	void RenderManager::drawPresentClear(const vk::CommandBuffer& commandBuffer, uint32_t imageIdx) const
	{
		// Fallback: clear swapchain image to magenta via dynamic rendering (debug: blit source not set)
		vk::Image swapchainImage = swapChain.getSwapchainImage(imageIdx);

		transitionSwapchainForRendering(commandBuffer, swapchainImage);

		auto colorAttach = colorClear(swapChain.getSwapchainImageView(imageIdx),
			vk::ClearColorValue{std::array<float, 4>{1.0f, 0.0f, 1.0f, 1.0f}});

		DynamicRenderingInfo dynInfo{};
		dynInfo.extent = swapChain.getSwapchainExtent();
		dynInfo.colorAttachments = {colorAttach};

		beginDynamicRendering(commandBuffer, dynInfo);
		endDynamicRendering(commandBuffer);

		transitionSwapchainForPresent(commandBuffer, swapchainImage);
	}

	void RenderManager::present(uint32_t imgIndex)
	{
		// Present the image to the screen
		vk::PresentInfoKHR presentInfo{};
		std::array<vk::Semaphore, 1> waitSemaphores = { renderFinishedSemaphores[imgIndex] };
		presentInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
		presentInfo.pWaitSemaphores = waitSemaphores.data();
		std::array<vk::SwapchainKHR, 1> swapChains = { swapChain.getSwapchain() };
		presentInfo.swapchainCount = static_cast<uint32_t>(swapChains.size());
		presentInfo.pSwapchains = swapChains.data();
		presentInfo.pImageIndices = &imgIndex;

		vk::Result result = device.getPresentQueue().presentKHR(&presentInfo);

		if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
			recreate(window->getHeight(), window->getWidth());
		}
	}

}
