#include "CoreInterface.hpp"
#include "../core/MainLoop.hpp"
#include "AnimatorSystemController.hpp"
#include "../../graphics/core/VulkanContext.hpp"
#include "../../graphics/core/SwapChain.hpp"

namespace controllers {

	CoreInterface::CoreInterface(bool imguiEnabled)
		: mainLoop{ std::make_unique<core::MainLoop>(imguiEnabled) }
		, animatorSystem{ std::make_unique<AnimatorSystemController>() }
	{
	}

	void CoreInterface::init()
	{
		mainLoop->init();
		animatorSystem->init();
	}

	void CoreInterface::run() const
	{
		mainLoop->run();
	}

	void CoreInterface::cleanUp()
	{
		animatorSystem->cleanUp();
		mainLoop->cleanUp();
	}

	void CoreInterface::closeWindow()
	{
		mainLoop->close();
	}

	CoreInterface::~CoreInterface() = default;

	window::Window* CoreInterface::getWindow() const
	{
		return mainLoop->getWindow();
	}

	void CoreInterface::setFrameCallback(std::function<void()> callback)
	{
		mainLoop->setFrameCallback(std::move(callback));
	}

	void CoreInterface::setPostUpdateCallback(std::function<void()> callback)
	{
		mainLoop->setPostUpdateCallback(std::move(callback));
	}

	void CoreInterface::setResizeCallback(std::function<void()> callback)
	{
		mainLoop->setResizeCallback(std::move(callback));
	}

	void CoreInterface::triggerResize()
	{
		mainLoop->triggerResize();
	}

	void CoreInterface::applyDisplaySettings(types::PresentMode presentMode, types::MsaaSamples /*msaa*/)
	{
		core::SwapChain* swapChain = core::VulkanContext::getSwapChain().get();
		if (!swapChain)
			return;

		vk::PresentModeKHR mode = vk::PresentModeKHR::eFifo;
		switch (presentMode)
		{
		case types::PresentMode::Fifo:      mode = vk::PresentModeKHR::eFifo; break;
		case types::PresentMode::Mailbox:   mode = vk::PresentModeKHR::eMailbox; break;
		case types::PresentMode::Immediate: mode = vk::PresentModeKHR::eImmediate; break;
		}

		// No-op if unchanged — avoids an unnecessary swapchain rebuild.
		if (swapChain->getDesiredPresentMode() == mode)
			return;

		swapChain->setDesiredPresentMode(mode);

		// MSAA is intentionally not applied yet (frame-graph integration pending).
		// When that lands: swapChain->setMSAASamples(<clamped>) goes here.

		mainLoop->triggerResize();
	}

	void CoreInterface::setBlitSourceProvider(std::function<void*(uint32_t)> provider)
	{
		mainLoop->setBlitSourceProvider(std::move(provider));
	}

	void CoreInterface::setPreRenderCallback(std::function<void()> callback)
	{
		mainLoop->setPreRenderCallback(std::move(callback));
	}

	void CoreInterface::stopRenderThread()
	{
		mainLoop->stopRenderThread();
	}

	std::function<void()> CoreInterface::getSceneGraphUpdateFn() const
	{
		return mainLoop->getSceneGraphUpdateFn();
	}

	std::function<void()> CoreInterface::getImguiDrawFn() const
	{
		return mainLoop->getImguiDrawFn();
	}

	std::function<void()> CoreInterface::getRenderFn() const
	{
		return mainLoop->getRenderFn();
	}

};
