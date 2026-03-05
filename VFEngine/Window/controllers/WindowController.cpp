#include "WindowController.hpp"
#include "../window/WindowContext.hpp"

namespace controllers {
	void WindowController::init(bool loadEditorIcon) {
		window::WindowContext::init(loadEditorIcon);
	}

	void WindowController::cleanUp() {
		window::WindowContext::cleanup();
	}

	window::Window* WindowController::getWindow() {
		return window::WindowContext::getWindow();
	}
}