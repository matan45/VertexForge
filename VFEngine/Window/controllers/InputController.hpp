#pragma once
#include <glm/glm.hpp>

// Forward declarations
struct GLFWwindow;

namespace window {
	class Window;

	// Input controller - Window layer API for input handling
	// Contains all GLFW-specific input logic
	class InputController {
	public:
		explicit InputController(Window* window);
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

		// Access to underlying window
		Window* getWindow() const { return window; }

	private:
		Window* window;
		GLFWwindow* glfwWindow;
	};

}
