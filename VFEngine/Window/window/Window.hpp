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
		int width{ 1024 };
		int height{ 768 };


		static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
		static void windowIconifyCallback(GLFWwindow* window, int iconified);
		static void windowFocusCallback(GLFWwindow* window, int focused);

	public:
		explicit Window() = default;
		~Window() = default;

		vk::SurfaceKHR createWindowSurface(const vk::UniqueInstance& instance) const;

		void initWindow(bool loadEditorIcon = true);
		void cleanup();
		void closeWindow();
		void pollEvents() const;
		bool shouldClose() const;
		
		bool isWindowResized() const { return isResized; }
		void resetResizeFlag() { isResized = false; }
		
		bool isWindowMinimized() const { return isMinimized; }
		bool hasMinimizeStateChanged() const { return minimizeStateChanged; }
		void resetMinimizeStateChanged() { minimizeStateChanged = false; }
		
		bool isWindowFocused() const { return isFocused; }
		bool hasFocusStateChanged() const { return focusStateChanged; }
		void resetFocusStateChanged() { focusStateChanged = false; }

		uint32_t getWidth() const { return width; }
		uint32_t getHeight() const { return height; }

		// Refresh rate (Hz) of the monitor the window is on (0 if unavailable).
		uint32_t getRefreshRate() const;

		GLFWwindow* getWindowPtr() const { return window; }

		void setTitle(const std::string& title);
		void setWindowIcon(std::string_view iconPath);
	};
}


