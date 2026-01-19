#include "CoreInterface.hpp"
#include "../core/MainLoop.hpp"
#include "AnimatorSystemController.hpp"

namespace controllers {

	CoreInterface::CoreInterface()
		: mainLoop{ std::make_unique<core::MainLoop>() }
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

	void CoreInterface::setResizeCallback(std::function<void()> callback)
	{
		mainLoop->setResizeCallback(std::move(callback));
	}

	void CoreInterface::triggerResize()
	{
		mainLoop->triggerResize();
	}

};