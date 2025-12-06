#pragma once
#include <memory>

namespace controllers {
	class RenderController;
}

namespace window {
	class Window;
}

namespace core {
	class MainLoop
	{
	private:
		std::unique_ptr<controllers::RenderController> renderController;
		window::Window* mainWindow;  // Non-owning pointer (owned by WindowController)
	public:
		explicit MainLoop();
		~MainLoop();

		void init();
		void run();
		void cleanUp() const;
		void close();

		// Get window pointer for service initialization
		window::Window* getWindow() const { return mainWindow; }

	private:
		void newFrame() const;
		void endFrame() const;
		void editorDraw() const;
	};

}

