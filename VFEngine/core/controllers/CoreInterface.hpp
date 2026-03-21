#pragma once
#include <memory>
#include <functional>

namespace core {
	class MainLoop;
}

namespace window {
	class Window;
	class InputController;
}

namespace controllers {

	class AnimatorSystemController;

	class CoreInterface
	{
	private:
		std::unique_ptr<core::MainLoop> mainLoop;
		std::unique_ptr<AnimatorSystemController> animatorSystem;

	public:
		explicit CoreInterface(bool imguiEnabled = true);
		~CoreInterface();

		void init();
		void run() const;
		void cleanUp();
		void closeWindow();

		// Get window pointer for service initialization
		window::Window* getWindow() const;

		// Set callback to be called each frame (for service updates)
		void setFrameCallback(std::function<void()> callback);

		// Set callback called after scene graph update (world transforms are valid)
		void setPostUpdateCallback(std::function<void()> callback);

		// Set callback to be called on resize (for offscreen resource recreation)
		void setResizeCallback(std::function<void()> callback);

		// Trigger window resize handling
		void triggerResize();

		// Set blit source provider for runtime (offscreen -> swapchain blit)
		void setBlitSourceProvider(std::function<void*(uint32_t)> provider);

		// Set callback to run on the render thread before swapchain present
		void setPreRenderCallback(std::function<void()> callback);

		// Get internal frame step functions for task graph orchestration
		std::function<void()> getSceneGraphUpdateFn() const;
		std::function<void()> getImguiDrawFn() const;
		std::function<void()> getRenderFn() const;
	};
}

