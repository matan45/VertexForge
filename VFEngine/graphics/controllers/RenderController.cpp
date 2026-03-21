#include "RenderController.hpp"
#include "../core/VulkanContext.hpp"
#include "../core/RenderManager.hpp"
#include "../core/RenderThread.hpp"
#include "../window/Window.hpp"
#include "print/Log.hpp"

namespace controllers {

	RenderController::RenderController(bool imguiEnabled)
		: window{ core::VulkanContext::getWindow() }
		, swapChain{ *core::VulkanContext::getSwapChain() }
		, device{ *core::VulkanContext::getDevice() }
		, renderManager{ std::make_unique<core::RenderManager>(device, swapChain, window, imguiEnabled) }
	{
	}

	void RenderController::render()
	{
		if (useRenderThread && renderThread)
		{
			// Signal the render thread that a new frame is ready
			renderThread->getSynchronizer().endFrame();
		}
		else
		{
			renderManager->render();
		}
	}

	void RenderController::beginFrame()
	{
		if (useRenderThread && renderThread)
		{
			renderThread->getSynchronizer().beginFrame();
		}
	}

	void RenderController::reSize()
	{
		if (useRenderThread && renderThread)
		{
			// Pause render thread during resize
			// The render thread will finish current frame, then block on waitForFrame
			// We need to signal it to stop, wait, then restart after resize
			// For now, use a simpler approach: wait for current frame to finish
			// by doing a beginFrame/endFrame cycle that ensures the slot is free
			device.getLogicalDevice().waitIdle();
		}

		swapChain.recreate(window->getWidth(), window->getHeight());
		renderManager->recreate(window->getWidth(), window->getHeight());
	}

	RenderController::~RenderController() = default;

	void RenderController::init()
	{
		renderManager->init();

		if (useRenderThread)
		{
			renderThread = std::make_unique<core::RenderThread>();
			renderThread->start([this](uint32_t /*frameSlot*/) {
				renderManager->render();
			});
			vfLogInfo("RenderController: Render thread enabled");
		}
	}

	void RenderController::cleanUp()
	{
		// Stop render thread BEFORE any Vulkan cleanup
		if (renderThread)
		{
			renderThread->stop();
			renderThread.reset();
		}

		renderManager->cleanUp();
	}

	void RenderController::setResizeCallback(core::ResizeCallback callback)
	{
		renderManager->setResizeCallback(std::move(callback));
	}

	void RenderController::setBlitSourceProvider(core::BlitSourceProvider provider)
	{
		renderManager->setBlitSourceProvider(std::move(provider));
	}

}
