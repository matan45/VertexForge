#include "CoreInterface.hpp"
#include "../core/MainLoop.hpp"
#include "AnimatorSystemController.hpp"
#include "../../graphics/core/VulkanContext.hpp"
#include "../../graphics/core/SwapChain.hpp"
#include "../../graphics/core/Device.hpp"

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

	void CoreInterface::applyDisplaySettings(types::PresentMode presentMode, types::MsaaSamples msaa)
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

		vk::SampleCountFlagBits requested = vk::SampleCountFlagBits::e1;
		switch (msaa)
		{
		case types::MsaaSamples::Off: requested = vk::SampleCountFlagBits::e1; break;
		case types::MsaaSamples::X2:  requested = vk::SampleCountFlagBits::e2; break;
		case types::MsaaSamples::X4:  requested = vk::SampleCountFlagBits::e4; break;
		case types::MsaaSamples::X8:  requested = vk::SampleCountFlagBits::e8; break;
		}

		// Clamp to what the device supports for both color and depth framebuffers.
		vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
		if (auto& device = core::VulkanContext::getDevice())
		{
			auto limits = device->getPhysicalDevice().getProperties().limits;
			vk::SampleCountFlags supported = limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts;
			for (auto candidate : {vk::SampleCountFlagBits::e8, vk::SampleCountFlagBits::e4, vk::SampleCountFlagBits::e2})
			{
				if (requested >= candidate && (supported & candidate))
				{
					samples = candidate;
					break;
				}
			}
		}

		// No-op if nothing changed — avoids an unnecessary swapchain + pipeline rebuild.
		if (swapChain->getDesiredPresentMode() == mode && swapChain->getMSAASamples() == samples)
			return;

		swapChain->setDesiredPresentMode(mode);
		swapChain->setMSAASamples(samples);

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
