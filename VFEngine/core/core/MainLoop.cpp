#include "MainLoop.hpp"
#include "Graphics.hpp"
#include "RenderController.hpp"
#include "WindowController.hpp"
#include "../window/Window.hpp"
#include "time/Timer.hpp"
#include "../controllers/imguiHandler/ImguiWindowHandler.hpp"
#include "resource/ResourceManager.hpp"
#include "scene/LevelHandler.hpp"
#include "threading/JobSystem.hpp"
#include "print/RuntimeDebugLog.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>
#include <ImGuizmo.h>

namespace core {

	MainLoop::MainLoop(bool imguiEnabled)
		: imguiEnabled{ imguiEnabled }
	{
		util::runtimeDebugLog("        MainLoop ctor - JobSystem::init()...");
		threading::JobSystem::instance().init();
		util::runtimeDebugLog("        MainLoop ctor - ResourceManager::init()...");
		resource::ResourceManager::init();
		util::runtimeDebugLog("        MainLoop ctor - WindowController::init()...");
		controllers::WindowController::init(imguiEnabled);
		mainWindow = controllers::WindowController::getWindow();
		util::runtimeDebugLog("        MainLoop ctor - Graphics::createContext()...");
		controllers::Graphics::createContext(mainWindow);
		util::runtimeDebugLog("        MainLoop ctor - RenderController()...");
		renderController = std::make_unique<controllers::RenderController>(imguiEnabled);
		util::runtimeDebugLog("        MainLoop ctor - done.");
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

			// Post-update callback runs after world transforms are computed
			if (postUpdateCallback) {
				postUpdateCallback();
			}

			if (imguiEnabled)
			{
				newFrame();
				editorDraw();
				endFrame();
			}

			renderController->render();
		}
	}

	void MainLoop::triggerResize()
	{
		renderController->reSize();
	}

	void MainLoop::setResizeCallback(std::function<void()> callback)
	{
		renderController->setResizeCallback(std::move(callback));
	}

	void MainLoop::cleanUp() const
	{
		renderController->cleanUp();
		controllers::Graphics::destroyContext();
		controllers::WindowController::cleanUp();
		resource::ResourceManager::cleanUp();
		threading::JobSystem::instance().shutdown();
	}


	void MainLoop::close()
	{
		mainWindow->closeWindow();
	}

	MainLoop::~MainLoop() = default;

	void MainLoop::setBlitSourceProvider(std::function<void*(uint32_t)> provider)
	{
		renderController->setBlitSourceProvider([p = std::move(provider)](uint32_t idx) -> vk::Image
		{
			VkImage raw = static_cast<VkImage>(p(idx));
			return vk::Image(raw);
		});
	}

	void MainLoop::newFrame() const
	{
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
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
