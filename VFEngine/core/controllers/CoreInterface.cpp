#include "CoreInterface.hpp"
#include "../core/MainLoop.hpp"
#include "AnimatorSystemController.hpp"
#include "print/RuntimeDebugLog.hpp"

namespace controllers {

	CoreInterface::CoreInterface(bool imguiEnabled)
		: mainLoop{ std::make_unique<core::MainLoop>(imguiEnabled) }
		, animatorSystem{ std::make_unique<AnimatorSystemController>() }
	{
		util::runtimeDebugLog("      CoreInterface constructed (imguiEnabled=" + std::string(imguiEnabled ? "true" : "false") + ")");
	}

	void CoreInterface::init()
	{
		util::runtimeDebugLog("      CoreInterface::init() - mainLoop->init()...");
		mainLoop->init();
		util::runtimeDebugLog("      CoreInterface::init() - mainLoop done, animatorSystem->init()...");
		animatorSystem->init();
		util::runtimeDebugLog("      CoreInterface::init() - done.");
	}

	void CoreInterface::run() const
	{
		mainLoop->run();
	}

	void CoreInterface::cleanUp() const
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

	void CoreInterface::setBlitSourceProvider(std::function<void*(uint32_t)> provider)
	{
		mainLoop->setBlitSourceProvider(std::move(provider));
	}

};