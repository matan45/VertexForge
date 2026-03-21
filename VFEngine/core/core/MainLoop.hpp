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

		// Frame callback - called each frame before scene graph update
		std::function<void()> frameCallback;
		// Post-update callback - called after scene graph update (world transforms computed)
		std::function<void()> postUpdateCallback;

		// Callbacks to expose MainLoop internals to the task graph
		std::function<void()> sceneGraphUpdateFn;
		std::function<void()> imguiDrawFn;
		std::function<void()> renderFn;
		std::function<void()> beginFrameFn;

	public:
		explicit MainLoop(bool imguiEnabled = true);
		~MainLoop();

		void init();
		void run();
		void cleanUp();
		void close();

		// Get window pointer for service initialization
		window::Window* getWindow() const { return mainWindow; }

		// Set callback to be called each frame (for service updates)
		void setFrameCallback(std::function<void()> callback) { frameCallback = std::move(callback); }

		// Set callback called after scene graph update (world transforms are valid)
		void setPostUpdateCallback(std::function<void()> callback) { postUpdateCallback = std::move(callback); }

		// Get internal step functions so the task graph can orchestrate the full frame
		std::function<void()> getSceneGraphUpdateFn() const { return sceneGraphUpdateFn; }
		std::function<void()> getImguiDrawFn() const { return imguiDrawFn; }
		std::function<void()> getRenderFn() const { return renderFn; }
		std::function<void()> getBeginFrameFn() const { return beginFrameFn; }

		// Set callback to be called on resize (for offscreen resource recreation)
		void setResizeCallback(std::function<void()> callback);

		// Trigger window resize handling (called by external event handlers)
		void triggerResize();

		// Set blit source provider for runtime (offscreen -> swapchain blit)
		void setBlitSourceProvider(std::function<void*(uint32_t)> provider);

	private:
		void newFrame() const;
		void endFrame() const;
		void editorDraw() const;
	};

}

