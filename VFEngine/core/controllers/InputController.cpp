#include "InputController.hpp"
#include "../window/Window.hpp"
#include <GLFW/glfw3.h>

namespace controllers {

	InputController::InputController(window::Window* window)
		: window(window)
		, glfwWindow(window ? window->getWindowPtr() : nullptr) {}

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

	void InputController::requestClose() {
		if (glfwWindow) {
			glfwSetWindowShouldClose(glfwWindow, GLFW_TRUE);
		}
	}

}
