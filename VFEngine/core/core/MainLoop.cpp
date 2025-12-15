#include "MainLoop.hpp"
#include "Graphics.hpp"
#include "RenderController.hpp"
#include "WindowController.hpp"
#include "../window/Window.hpp"
#include "time/Timer.hpp"
#include "../controllers/imguiHandler/ImguiWindowHandler.hpp"
#include "resource/ResourceManager.hpp"
#include "scene/LevelHandler.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>

namespace core {

	MainLoop::MainLoop()
	{
		resource::ResourceManager::init();
		controllers::WindowController::init();
		mainWindow = controllers::WindowController::getWindow();
		controllers::Graphics::createContext(mainWindow);
		renderController = std::make_unique<controllers::RenderController>();
	}

	void MainLoop::init()
	{
		renderController->init();
		engineTime::Timer::initialize();
	}

	void MainLoop::run()
	{
		while (!mainWindow->shouldClose()) {
			mainWindow->pollEvents();

			engineTime::Timer::update();

			// Call frame callback (updates services, publishes events)
			if (frameCallback) {
				frameCallback();
			}

			scene::LevelHandler::update();

			newFrame();
			editorDraw();
			endFrame();

			renderController->render();
		}
	}

	void MainLoop::triggerResize()
	{
		renderController->reSize();
	}

	void MainLoop::cleanUp() const
	{
		renderController->cleanUp();
		controllers::Graphics::destroyContext();
		controllers::WindowController::cleanUp();
		resource::ResourceManager::cleanUp();
	}


	void MainLoop::close()
	{
		mainWindow->closeWindow();
	}

	MainLoop::~MainLoop() = default;

	void MainLoop::newFrame() const
	{
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	void MainLoop::endFrame() const
	{
		ImGui::EndFrame();
	}

	void MainLoop::editorDraw() const
	{
		controllers::imguiHandler::ImguiWindowHandler::draw();
	}

}
