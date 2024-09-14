#include "Window.hpp"
#include "log/Logger.hpp"


namespace window {
	void Window::initWindow()
	{
		if (!glfwInit())
			loggerError("Unable to initialize GLFW");

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

		window = glfwCreateWindow(WIDTH, HEIGHT, "engine test", nullptr, nullptr);

		if (window == nullptr)
			loggerError("Failed to create GLFW window");

		glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
	}

	void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height)
	{
		WIDTH = width;
		HEIGHT = height;
		ISRESIZE = true;
	}

	Window::Window()
	{
		WIDTH = 800;
		HEIGHT = 600;
		ISRESIZE = false;
	}

	void Window::createWindowSurface(const vk::UniqueInstance& instance, vk::SurfaceKHR& surface)
	{
		VkSurfaceKHR rawSurface;
		VkResult result = glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface);
		loggerAssert(result != VK_SUCCESS,
			"failed to create window surface!");

		surface = vk::SurfaceKHR(rawSurface);
	}

	void Window::cleanup()
	{
		glfwDestroyWindow(window);
		glfwTerminate();
	}

	void Window::pollEvents() const
	{
		glfwPollEvents();
	}

	bool Window::shouldClose() const
	{
		return glfwWindowShouldClose(window);
	}

}

