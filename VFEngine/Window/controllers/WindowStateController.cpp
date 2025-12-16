#include "WindowStateController.hpp"
#include "../window/Window.hpp"

namespace window {

	WindowStateController::WindowStateController(Window* window)
		: window(window) {}

	bool WindowStateController::isWindowResized() const {
		return window ? window->isWindowResized() : false;
	}

	void WindowStateController::resetResizeFlag() {
		if (window) window->resetResizeFlag();
	}

	uint32_t WindowStateController::getWindowWidth() const {
		return window ? window->getWidth() : 0;
	}

	uint32_t WindowStateController::getWindowHeight() const {
		return window ? window->getHeight() : 0;
	}

	bool WindowStateController::isWindowMinimized() const {
		return window ? window->isWindowMinimized() : false;
	}

	bool WindowStateController::hasMinimizeStateChanged() const {
		return window ? window->hasMinimizeStateChanged() : false;
	}

	void WindowStateController::resetMinimizeStateChanged() {
		if (window) window->resetMinimizeStateChanged();
	}

	bool WindowStateController::isWindowFocused() const {
		return window ? window->isWindowFocused() : true;
	}

	bool WindowStateController::hasFocusStateChanged() const {
		return window ? window->hasFocusStateChanged() : false;
	}

	void WindowStateController::resetFocusStateChanged() {
		if (window) window->resetFocusStateChanged();
	}

}
