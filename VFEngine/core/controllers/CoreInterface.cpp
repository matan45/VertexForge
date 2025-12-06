#include "CoreInterface.hpp"
#include "../core/MainLoop.hpp"

namespace controllers {

	CoreInterface::CoreInterface()
		: mainLoop{ std::make_unique<core::MainLoop>() }
	{
	}

	void CoreInterface::init()
	{
		mainLoop->init();
	}

	void CoreInterface::run() const
	{
		mainLoop->run();
	}

	void CoreInterface::cleanUp() const
	{
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

};