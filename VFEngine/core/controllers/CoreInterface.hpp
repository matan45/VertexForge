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
		
	public:
		explicit CoreInterface();
		~CoreInterface();

		void init();
		void run() const;
		void cleanUp() const;
		void closeWindow();

		// Get window pointer for service initialization
		window::Window* getWindow() const;
	};
}

