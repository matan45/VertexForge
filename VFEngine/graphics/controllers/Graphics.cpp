#include "Graphics.hpp"
#include "../window/Window.hpp"
#include "print/Log.hpp"
#include "../core/VulkanContext.hpp"


namespace controllers
{

	void Graphics::createContext(window::Window* glfwWindow, bool enablePipelineCache)
	{
		core::VulkanContext::init(glfwWindow, enablePipelineCache);
	}

	void Graphics::destroyContext()
	{
		core::VulkanContext::cleanup();

	}

};

