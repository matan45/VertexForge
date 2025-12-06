#include "RenderManager.hpp"
#include "Device.hpp"
#include "SwapChain.hpp"
#include "CommandPool.hpp"
#include "print/Logger.hpp"
#include "../window/Window.hpp"
#include "../imguiPass/ImguiRender.hpp"

namespace core {


	RenderManager::RenderManager(Device& device, SwapChain& swapChain,const window::Window* window) : device{ device },
		swapChain{ swapChain }, window{ window }
	{

	}

	RenderManager::~RenderManager() = default;

	void RenderManager::init()
	{
		commandPool = std::make_unique<CommandPool>(device, swapChain);

		imguiRender = std::make_unique<imguiPass::ImguiRender>(device, swapChain, *commandPool, window);
		imguiRender->init();

		// Create semaphores for synchronization
		vk::SemaphoreCreateInfo semaphoreInfo{};

		imageAvailableSemaphore = device.getLogicalDevice().createSemaphore(semaphoreInfo);
		renderFinishedSemaphore = device.getLogicalDevice().createSemaphore(semaphoreInfo);

		// Create a fence for GPU-CPU synchronization
		vk::FenceCreateInfo fenceInfo{};
		renderFence = device.getLogicalDevice().createFence(fenceInfo);
	}

	void RenderManager::render()
	{
		// Acquire the next image from the swapchain
		vk::Result result = device.getLogicalDevice().acquireNextImageKHR(
			swapChain.getSwapchain(),
			UINT64_MAX,
			imageAvailableSemaphore,
			nullptr,
			&imageIndex
		);

		if (result == vk::Result::eErrorOutOfDateKHR) {
			recreate(window->getHeight(), window->getWidth());  // Handle swapchain recreation
			return;
		}

		// Reset the fence before using it for synchronization
		result = device.getLogicalDevice().resetFences(1, &renderFence);
		if (result != vk::Result::eSuccess) {
			loggerError("failed to reset fences");
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
		vk::SubmitInfo submitInfo{};
		std::array <vk::Semaphore, 1> waitSemaphores = { imageAvailableSemaphore };
		std::array < vk::Semaphore, 1> signalSemaphores = { renderFinishedSemaphore };
		std::array < vk::PipelineStageFlags, 1> waitStages = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

		//need also get here the offscreen command buffer
		vk::CommandBuffer cmdBuffer = commandPool->getCommandBuffer(imageIndex);
		submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
		submitInfo.pWaitSemaphores = waitSemaphores.data();
		submitInfo.pWaitDstStageMask = waitStages.data();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdBuffer;
		submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
		submitInfo.pSignalSemaphores = signalSemaphores.data();

		device.getGraphicsQueue().submit(submitInfo, renderFence);

		// Wait for the GPU to finish executing the command buffer before continuing
		result = device.getLogicalDevice().waitForFences(1, &renderFence, VK_TRUE, UINT64_MAX);
		if (result != vk::Result::eSuccess) {
			loggerError("failed to wait fences");
		}

		// Present the rendered image
		present();

	}

	void RenderManager::recreate(uint32_t width, uint32_t height) const
	{
		if (width == 0 || height == 0) return;  // Skip if minimized
		commandPool->recreate();  // Reallocate command buffers if needed

		imguiRender->recreate();
	}

	void RenderManager::cleanUp() const
	{
		device.getLogicalDevice().waitIdle();

		commandPool->cleanUp();

		imguiRender->cleanUp();

		device.getLogicalDevice().destroySemaphore(imageAvailableSemaphore);
		device.getLogicalDevice().destroySemaphore(renderFinishedSemaphore);
		device.getLogicalDevice().destroyFence(renderFence);
	}

	void RenderManager::draw(const vk::CommandBuffer& commandBuffer) const
	{
		//create a render pass class so we can use here and offscreen class
		//here for now only use imgui render
		imguiRender->render(commandBuffer, imageIndex);
	}

	void RenderManager::present() const
	{
		// Present the image to the screen
		vk::PresentInfoKHR presentInfo{};
		std::array<vk::Semaphore, 1> waitSemaphores = { renderFinishedSemaphore };
		presentInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
		presentInfo.pWaitSemaphores = waitSemaphores.data();
		std::array <vk::SwapchainKHR, 1> swapChains = { swapChain.getSwapchain() };
		presentInfo.swapchainCount = static_cast<uint32_t>(swapChains.size());
		presentInfo.pSwapchains = swapChains.data();
		presentInfo.pImageIndices = &imageIndex;

		vk::Result result = device.getPresentQueue().presentKHR(&presentInfo);

		if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
			recreate(window->getHeight(), window->getWidth());
		}
	}

}

