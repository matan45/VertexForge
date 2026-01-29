#include "RenderManager.hpp"
#include "Device.hpp"
#include "SwapChain.hpp"
#include "CommandPool.hpp"
#include "DeferredDeletionQueue.hpp"
#include "print/Logger.hpp"
#include "../window/Window.hpp"
#include "../imguiPass/ImguiRender.hpp"

namespace core {


	RenderManager::RenderManager(Device& device, SwapChain& swapChain,const window::Window* window) : device{ device },
		swapChain{ swapChain }, window{ window }
	{

	}

	RenderManager::~RenderManager()
	{
		globalDeletionQueue = nullptr;
	}

	void RenderManager::init()
	{
		commandPool = std::make_unique<CommandPool>(device, swapChain);

		imguiRender = std::make_unique<imguiPass::ImguiRender>(device, swapChain, *commandPool, window);
		imguiRender->init();

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
			loggerError("failed to wait for in-flight fence");
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
				loggerError("failed to wait for image in flight fence");
			}
		}
		// Mark this image as now being in use by this frame
		imagesInFlight[imageIndex] = inFlightFences[currentFrame];

		// Reset the fence only after we know we will submit work
		result = device.getLogicalDevice().resetFences(1, &inFlightFences[currentFrame]);
		if (result != vk::Result::eSuccess) {
			loggerError("failed to reset fence");
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

	void RenderManager::recreate(uint32_t width, uint32_t height) const
	{
		if (width == 0 || height == 0) return;  // Skip if minimized

		// Wait for all GPU work to complete before recreating resources
		device.getLogicalDevice().waitIdle();

		commandPool->recreate();  // Reallocate command buffers if needed

		imguiRender->recreate();

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

		imguiRender->cleanUp();

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
		//create a render pass class so we can use here and offscreen class
		//here for now only use imgui render
		imguiRender->render(commandBuffer, imageIndex);
	}

	void RenderManager::present(uint32_t frameIndex) const
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

