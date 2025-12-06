#pragma once

namespace core {
	class MainLoop;
}

namespace window {
	class Window;
}

namespace controllers {

	class CoreInterface
	{
	private:
		core::MainLoop* mainLoop;
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

