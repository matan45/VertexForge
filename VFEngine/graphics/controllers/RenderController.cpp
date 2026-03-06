#include "RenderController.hpp"
#include "../core/VulkanContext.hpp"
#include "../core/RenderManager.hpp"
#include "../window/Window.hpp"

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
		renderManager->render();
	}

	void RenderController::reSize()
	{
		//handle resize window
		swapChain.recreate(window->getWidth(), window->getHeight());
		renderManager->recreate(window->getWidth(), window->getHeight());
	}
	

	RenderController::~RenderController() = default;

	void RenderController::init()
	{
		renderManager->init();
	}

	void RenderController::cleanUp() const
	{
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
