#pragma once
#include <memory>
#include <functional>

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
		bool imguiEnabled;
		std::unique_ptr<controllers::RenderController> renderController;
		window::Window* mainWindow;  // Non-owning pointer (owned by WindowController)

		// Frame callback - called each frame before rendering
		std::function<void()> frameCallback;

	public:
		explicit MainLoop(bool imguiEnabled = true);
		~MainLoop();

		void init();
		void run();
		void cleanUp() const;
		void close();

		// Get window pointer for service initialization
		window::Window* getWindow() const { return mainWindow; }

		// Set callback to be called each frame (for service updates)
		void setFrameCallback(std::function<void()> callback) { frameCallback = std::move(callback); }

		// Set callback to be called on resize (for offscreen resource recreation)
		void setResizeCallback(std::function<void()> callback);

		// Trigger window resize handling (called by external event handlers)
		void triggerResize();

	private:
		void newFrame() const;
		void endFrame() const;
		void editorDraw() const;
	};

}

