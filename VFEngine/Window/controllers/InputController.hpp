#pragma once
#include <glm/glm.hpp>
#include <unordered_map>

struct GLFWwindow;

namespace window {
	class Window;

	
	class InputController {
	private:
		Window* window;
		GLFWwindow* glfwWindow;

		// Mouse delta tracking
		glm::vec2 lastMousePos{ 0.0f };
		glm::vec2 mouseDelta{ 0.0f };
		bool firstMouseUpdate{ true };

		// Scroll delta tracking (accumulated between frames)
		glm::vec2 scrollDelta{ 0.0f };

		// Double-click detection
		static constexpr double DOUBLE_CLICK_TIME = 0.3; // seconds
		static constexpr float DOUBLE_CLICK_DISTANCE = 5.0f; // pixels
		double lastClickTime[8]{ 0.0 }; // per button
		glm::vec2 lastClickPos[8]{ glm::vec2(0.0f) };
		bool wasButtonDown[8]{ false };
		bool doubleClickDetected[8]{ false };

		// Static registry mapping GLFW windows to InputController instances.
		// Thread Safety: Only accessed from main thread where GLFW callbacks execute.
		// GLFW requires all window operations on the main thread, so no synchronization needed.
		inline static std::unordered_map<GLFWwindow*, InputController*> controllerRegistry;
	public:
		explicit InputController(Window* window);
		~InputController();

		// Keyboard State
		bool isKeyDown(int keyCode) const;
		bool isKeyReleased(int keyCode) const;

		// Mouse State
		bool isMouseButtonDown(int button) const;
		bool isMouseButtonReleased(int button) const;
		bool isDoubleClick(int button) const;
		glm::vec2 getMousePosition() const;
		void getCursorPos(double& xpos, double& ypos) const;
		glm::vec2 getMouseDelta() const;
		glm::vec2 getScrollDelta() const;

		// Frame update - must be called once per frame to track deltas
		void update();
		
		Window* getWindow() const { return window; }

		// GLFW callback handler (called internally by scroll callback)
		void onScroll(double xoffset, double yoffset);
		
		static InputController* getControllerForWindow(GLFWwindow* window);
	};

}
