#pragma once
#include <glm/glm.hpp>

// Forward declarations
struct GLFWwindow;

namespace window {
	class Window;
}

namespace controllers {

	// Input controller - Core API for input handling
	// Wraps Window/GLFW input to provide unified input API
	class InputController {
	public:
		explicit InputController(window::Window* window);
		~InputController() = default;

		// Keyboard State
		bool isKeyDown(int keyCode) const;
		bool isKeyReleased(int keyCode) const;

		// Mouse State
		bool isMouseButtonDown(int button) const;
		bool isMouseButtonReleased(int button) const;
		glm::vec2 getMousePosition() const;
		void getCursorPos(double& xpos, double& ypos) const;

		// Application Control
		void requestClose();

		// Access to underlying window (for initialization)
		window::Window* getWindow() const { return window; }

	private:
		window::Window* window;
		GLFWwindow* glfwWindow;
	};

}
