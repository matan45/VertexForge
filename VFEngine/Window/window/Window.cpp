#include "Window.hpp"
#include "resource/ResourceManager.hpp"
#include "print/Logger.hpp"


namespace window {
	void Window::initWindow()
	{
		if (!glfwInit()) {
			loggerError("Unable to initialize GLFW");
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

		window = glfwCreateWindow(width, height, "VertexForge", nullptr, nullptr);

		if (window == nullptr) {
			loggerError("Failed to create GLFW window");
		}

		glfwSetWindowUserPointer(window, this); // Set the user pointer to access the class
		glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
		glfwSetWindowIconifyCallback(window, windowIconifyCallback);
		glfwSetWindowFocusCallback(window, windowFocusCallback);

		setWindowIcon("../../resources/editor/window-icon.vfImage");
	}

	void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height)
	{
		auto userWindow = static_cast<Window*>(glfwGetWindowUserPointer(window));
		userWindow->width = width;
		userWindow->height = height;
		userWindow->isResized = true;
	}

	void Window::windowIconifyCallback(GLFWwindow* window, int iconified)
	{
		auto userWindow = static_cast<Window*>(glfwGetWindowUserPointer(window));
		userWindow->isMinimized = (iconified == GLFW_TRUE);
		userWindow->minimizeStateChanged = true;
	}

	void Window::windowFocusCallback(GLFWwindow* window, int focused)
	{
		auto userWindow = static_cast<Window*>(glfwGetWindowUserPointer(window));
		userWindow->isFocused = (focused == GLFW_TRUE);
		userWindow->focusStateChanged = true;
	}

	vk::SurfaceKHR Window::createWindowSurface(const vk::UniqueInstance& instance) const
	{
		VkSurfaceKHR rawSurface;
		VkResult result = glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface);
		loggerAssert(result != VK_SUCCESS,
			"failed to create window surface!");

		return vk::SurfaceKHR(rawSurface);
	}

	void Window::cleanup()
	{
		glfwDestroyWindow(window);
		glfwTerminate();
	}

	void Window::closeWindow()
	{
		glfwSetWindowShouldClose(window, true);
	}

	void Window::pollEvents() const
	{
		glfwPollEvents();
	}

	bool Window::shouldClose() const
	{
		return glfwWindowShouldClose(window);
	}

	void Window::setWindowIcon(std::string_view iconPath) {
		auto iconData = resource::ResourceManager::loadTextureAsync(iconPath);

		auto dataPtr = iconData.get();
		// Create GLFWimage and assign the loaded image data
		GLFWimage icon;
		icon.width = static_cast<int>(dataPtr->width);
		icon.height = static_cast<int>(dataPtr->height);
		icon.pixels = const_cast<unsigned char*>(dataPtr->textureData().data());

		// Set the icon for the GLFW window
		glfwSetWindowIcon(window, 1, &icon);
	}

}

