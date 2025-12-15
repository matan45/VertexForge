#pragma once
#include <glm/glm.hpp>
#include <unordered_map>
#include <cstdint>

struct GLFWwindow;

namespace window {
	class Window;

	// Input controller - Window layer API for input handling
	// Contains all GLFW-specific input logic
	class InputController {
	public:
		explicit InputController(Window* window);
		~InputController();

		// Keyboard State
		bool isKeyDown(int keyCode) const;
		bool isKeyReleased(int keyCode) const;

		// Mouse State
		bool isMouseButtonDown(int button) const;
		bool isMouseButtonReleased(int button) const;
		glm::vec2 getMousePosition() const;
		void getCursorPos(double& xpos, double& ypos) const;
		glm::vec2 getMouseDelta() const;
		glm::vec2 getScrollDelta() const;

		// Window State (delegates to Window)
		bool isWindowResized() const;
		void resetResizeFlag();
		bool isWindowMinimized() const;
		bool hasMinimizeStateChanged() const;
		void resetMinimizeStateChanged();
		bool isWindowFocused() const;
		bool hasFocusStateChanged() const;
		void resetFocusStateChanged();
		uint32_t getWindowWidth() const;
		uint32_t getWindowHeight() const;

		// Frame update - must be called once per frame to track deltas
		void update();

		// Application Control
		void requestClose();

		// Access to underlying window
		Window* getWindow() const { return window; }

		// GLFW callback handler (called internally by scroll callback)
		void onScroll(double xoffset, double yoffset);

		// Static lookup for scroll callback (avoids conflicting with Window's user pointer)
		static InputController* getControllerForWindow(GLFWwindow* window);

	private:
		Window* window;
		GLFWwindow* glfwWindow;

		// Mouse delta tracking
		glm::vec2 lastMousePos{ 0.0f };
		glm::vec2 mouseDelta{ 0.0f };
		bool firstMouseUpdate{ true };

		// Scroll delta tracking (accumulated between frames)
		glm::vec2 scrollDelta{ 0.0f };

		// Static registry mapping GLFW windows to InputController instances.
		// Thread Safety: Only accessed from main thread where GLFW callbacks execute.
		// GLFW requires all window operations on the main thread, so no synchronization needed.
		inline static std::unordered_map<GLFWwindow*, InputController*> controllerRegistry;
	};

}
