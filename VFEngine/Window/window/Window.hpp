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
		bool isResized{ false }; // Non-static resize flag
		int width{ 800 };
		int height{ 600 };


		static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

	public:
		explicit Window() = default;
		~Window() = default;

		vk::SurfaceKHR createWindowSurface(const vk::UniqueInstance& instance) const;

		void initWindow();
		void cleanup();
		void closeWindow();
		void pollEvents() const;
		bool shouldClose() const;
		bool isWindowResized() const { return isResized; }
		void resetResizeFlag() { isResized = false; }

		uint32_t getWidth() const { return width; }
		uint32_t getHeight() const { return height; }

		GLFWwindow* getWindowPtr() const { return window; }

	private:
		void setWindowIcon(std::string_view iconPath);
	};
}


