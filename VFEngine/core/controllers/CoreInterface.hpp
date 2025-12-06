#pragma once
#include <memory>

namespace core {
	class MainLoop;
}

namespace window {
	class Window;
	class InputController;
}

namespace controllers {

	class CoreInterface
	{
	private:
		std::unique_ptr<core::MainLoop> mainLoop;
		std::unique_ptr<window::InputController> inputController;
	public:
		explicit CoreInterface();
		~CoreInterface();

		void init();
		void run() const;
		void cleanUp() const;
		void closeWindow();

		// Get window pointer for service initialization
		window::Window* getWindow() const;

		// Get input controller for service initialization
		window::InputController* getInputController() const { return inputController.get(); }
	};
}

