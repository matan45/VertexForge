#include "CoreInterface.hpp"
#include "../core/MainLoop.hpp"

namespace controllers {

	CoreInterface::CoreInterface() : mainLoop{ new core::MainLoop() }
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

	CoreInterface::~CoreInterface()
	{
		delete mainLoop;
	}

	window::Window* CoreInterface::getWindow() const
	{
		return mainLoop->getWindow();
	}

};