#pragma once
#define GLFW_INCLUDE_VULKAN
#include <vulkan/vulkan.hpp>
#include "GLFW/glfw3.h"
#include <string_view>
//TODO extrect to onther lib
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

		void createWindowSurface(const vk::UniqueInstance& instance, vk::SurfaceKHR& surface);

		void initWindow();
		void cleanup();
		void pollEvents() const;
		bool shouldClose() const;
		bool isWindowResized() const { return isResized; }
		void resetResizeFlag() { isResized = false; }

		uint32_t getWidth() const { return width; }
		uint32_t getHeight() const { return height; }

		GLFWwindow* getWindowPtr() const { return window;}

	private:
		void setWindowIcon(std::string_view iconPath);
	};
}


