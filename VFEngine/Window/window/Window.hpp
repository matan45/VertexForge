#pragma once
#define GLFW_INCLUDE_VULKAN
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include "GLFW/glfw3.h"
#include <string_view>


namespace window {
	class Window
	{
	private:
		GLFWwindow* window{ nullptr };
		bool isResized{ false };
		bool isMinimized{ false };
		bool isFocused{ true };
		bool minimizeStateChanged{ false };
		bool focusStateChanged{ false };
		int width{ 800 };
		int height{ 600 };


		static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
		static void windowIconifyCallback(GLFWwindow* window, int iconified);
		static void windowFocusCallback(GLFWwindow* window, int focused);

	public:
		explicit Window() = default;
		~Window() = default;

		vk::SurfaceKHR createWindowSurface(const vk::UniqueInstance& instance) const;

		void initWindow();
		void cleanup();
		void closeWindow();
		void pollEvents() const;
		bool shouldClose() const;

		// Resize state
		bool isWindowResized() const { return isResized; }
		void resetResizeFlag() { isResized = false; }

		// Minimize state
		bool isWindowMinimized() const { return isMinimized; }
		bool hasMinimizeStateChanged() const { return minimizeStateChanged; }
		void resetMinimizeStateChanged() { minimizeStateChanged = false; }

		// Focus state
		bool isWindowFocused() const { return isFocused; }
		bool hasFocusStateChanged() const { return focusStateChanged; }
		void resetFocusStateChanged() { focusStateChanged = false; }

		uint32_t getWidth() const { return width; }
		uint32_t getHeight() const { return height; }

		GLFWwindow* getWindowPtr() const { return window; }

	private:
		void setWindowIcon(std::string_view iconPath);
	};
}


