#include "InputController.hpp"
#include "../window/Window.hpp"
#include <GLFW/glfw3.h>

namespace window {

	// Static registry definition
	std::unordered_map<GLFWwindow*, InputController*> InputController::controllerRegistry;

	// Static callback function for GLFW scroll events
	static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
		auto* controller = InputController::getControllerForWindow(window);
		if (controller) {
			controller->onScroll(xoffset, yoffset);
		}
	}

	InputController* InputController::getControllerForWindow(GLFWwindow* window) {
		auto it = controllerRegistry.find(window);
		return (it != controllerRegistry.end()) ? it->second : nullptr;
	}

	InputController::InputController(Window* window)
		: window(window)
		, glfwWindow(window ? window->getWindowPtr() : nullptr) {
		if (glfwWindow) {
			// Register in static map (doesn't conflict with Window's user pointer)
			controllerRegistry[glfwWindow] = this;
			// Register scroll callback
			glfwSetScrollCallback(glfwWindow, scrollCallback);
		}
	}

	InputController::~InputController() {
		if (glfwWindow) {
			// Clear the scroll callback
			glfwSetScrollCallback(glfwWindow, nullptr);
			// Remove from registry
			controllerRegistry.erase(glfwWindow);
		}
	}

	bool InputController::isKeyDown(int keyCode) const {
		if (!glfwWindow) return false;
		return glfwGetKey(glfwWindow, keyCode) == GLFW_PRESS;
	}

	bool InputController::isKeyReleased(int keyCode) const {
		if (!glfwWindow) return false;
		return glfwGetKey(glfwWindow, keyCode) == GLFW_RELEASE;
	}

	bool InputController::isMouseButtonDown(int button) const {
		if (!glfwWindow) return false;
		return glfwGetMouseButton(glfwWindow, button) == GLFW_PRESS;
	}

	bool InputController::isMouseButtonReleased(int button) const {
		if (!glfwWindow) return false;
		return glfwGetMouseButton(glfwWindow, button) == GLFW_RELEASE;
	}

	glm::vec2 InputController::getMousePosition() const {
		if (!glfwWindow) return glm::vec2(0.0f);

		double xpos, ypos;
		glfwGetCursorPos(glfwWindow, &xpos, &ypos);
		return glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));
	}

	void InputController::getCursorPos(double& xpos, double& ypos) const {
		if (!glfwWindow) {
			xpos = 0.0;
			ypos = 0.0;
			return;
		}
		glfwGetCursorPos(glfwWindow, &xpos, &ypos);
	}

	glm::vec2 InputController::getMouseDelta() const {
		return mouseDelta;
	}

	glm::vec2 InputController::getScrollDelta() const {
		return scrollDelta;
	}

	void InputController::update() {
		// Calculate mouse delta
		glm::vec2 currentPos = getMousePosition();

		if (firstMouseUpdate) {
			lastMousePos = currentPos;
			firstMouseUpdate = false;
			mouseDelta = glm::vec2(0.0f);
		}
		else {
			mouseDelta = currentPos - lastMousePos;
			lastMousePos = currentPos;
		}

		// Reset scroll delta after it's been consumed
		// (scrollDelta is accumulated by callback between frames)
		scrollDelta = glm::vec2(0.0f);
	}

	void InputController::onScroll(double xoffset, double yoffset) {
		// Accumulate scroll delta (can have multiple scroll events per frame)
		scrollDelta.x += static_cast<float>(xoffset);
		scrollDelta.y += static_cast<float>(yoffset);
	}

	void InputController::requestClose() {
		if (glfwWindow) {
			glfwSetWindowShouldClose(glfwWindow, GLFW_TRUE);
		}
	}

	// Window state delegation methods
	bool InputController::isWindowResized() const {
		return window ? window->isWindowResized() : false;
	}

	void InputController::resetResizeFlag() {
		if (window) window->resetResizeFlag();
	}

	bool InputController::isWindowMinimized() const {
		return window ? window->isWindowMinimized() : false;
	}

	bool InputController::hasMinimizeStateChanged() const {
		return window ? window->hasMinimizeStateChanged() : false;
	}

	void InputController::resetMinimizeStateChanged() {
		if (window) window->resetMinimizeStateChanged();
	}

	bool InputController::isWindowFocused() const {
		return window ? window->isWindowFocused() : true;
	}

	bool InputController::hasFocusStateChanged() const {
		return window ? window->hasFocusStateChanged() : false;
	}

	void InputController::resetFocusStateChanged() {
		if (window) window->resetFocusStateChanged();
	}

	uint32_t InputController::getWindowWidth() const {
		return window ? window->getWidth() : 0;
	}

	uint32_t InputController::getWindowHeight() const {
		return window ? window->getHeight() : 0;
	}

}
