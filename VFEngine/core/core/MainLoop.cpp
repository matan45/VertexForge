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
#include "../../graphics/render/upscaling/UpscaleManager.hpp"

#include <thread>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>
#include <ImGuizmo.h>

namespace core {

	MainLoop::MainLoop(bool imguiEnabled)
		: imguiEnabled{ imguiEnabled }
	{
		threading::JobSystem::instance().init();
		resource::ResourceManager::init();
		controllers::WindowController::init(imguiEnabled);
		mainWindow = controllers::WindowController::getWindow();
		controllers::Graphics::createContext(mainWindow, !imguiEnabled);
		renderController = std::make_unique<controllers::RenderController>(imguiEnabled);
	}

	void MainLoop::init()
	{
		renderController->init();
		engineTime::Timer::initialize();

		// Expose internal steps so the task graph can orchestrate the full frame
		sceneGraphUpdateFn = []() { scene::LevelHandler::update(); };

		if (imguiEnabled) {
			imguiDrawFn = [this]() {
				newFrame();
				editorDraw();
				endFrame();
			};
		}

		renderFn = [this]() {
			renderController->render();
		};
	}

	void MainLoop::run()
	{
		while (!mainWindow->shouldClose()) {
			// Wait for the render thread to finish the previous frame BEFORE sampling input
			// and time. Under FIFO VSync this wait is up to a full frame long; sampling input
			// and the frame delta *after* the wait (not before) keeps the frame we simulate
			// aligned with the frame actually being produced, so editor/camera movement is
			// smooth instead of stepped (VK-1428). Also ensures descriptor sets aren't updated
			// while the GPU is still using them.
			renderController->beginFrame();

			// NVIDIA Reflex: mint this frame's token, mark input/latency ping, then sleep
			// as early as possible to reduce render/input latency. No-ops when Reflex inactive.
			auto* reflex = render::upscaling::UpscaleManager::getMutableInstance();
			if (reflex) {
				reflex->beginReflexFrame();
				reflex->setMarkerMain(render::upscaling::UpscaleManager::FrameMarker::InputPing);
				reflex->reflexSleep();
			}

			mainWindow->pollEvents();

			engineTime::Timer::update();

			// The frame callback orchestrates the entire frame pipeline
			// (service updates, scene graph, post-update, imgui, render)
			if (reflex)
				reflex->setMarkerMain(render::upscaling::UpscaleManager::FrameMarker::SimulationStart);
			if (frameCallback) {
				// Publish the frame index before frameCallback kicks the render thread,
				// so the render thread re-fetches the matching Reflex token.
				if (reflex)
					reflex->publishRenderFrameIndex();
				frameCallback();
			}
			if (reflex)
				reflex->setMarkerMain(render::upscaling::UpscaleManager::FrameMarker::SimulationEnd);
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

	void MainLoop::stopRenderThread()
	{
		renderController->stopRenderThread();
	}

	void MainLoop::cleanUp()
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

	void MainLoop::setPreRenderCallback(std::function<void()> callback)
	{
		renderController->setPreRenderCallback(std::move(callback));
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
