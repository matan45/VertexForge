#pragma once
#include <cstdint>

namespace window {
	class Window;

	class WindowStateController {
	private:
		Window* window;
	public:
		explicit WindowStateController(Window* window);
		~WindowStateController() = default;

		// Resize State
		bool isWindowResized() const;
		void resetResizeFlag();
		uint32_t getWindowWidth() const;
		uint32_t getWindowHeight() const;

		// Minimize State
		bool isWindowMinimized() const;
		bool hasMinimizeStateChanged() const;
		void resetMinimizeStateChanged();

		// Focus State
		bool isWindowFocused() const;
		bool hasFocusStateChanged() const;
		void resetFocusStateChanged();

		// Access to underlying window
		Window* getWindow() const { return window; }
	};

}
