#include "RenderController.hpp"
#include "../core/VulkanContext.hpp"
#include "../core/RenderManager.hpp"
#include "../core/RenderThread.hpp"
#include "../window/Window.hpp"
#include "threading/TaskProfiler.hpp"
#include "print/Log.hpp"
#include <chrono>

namespace controllers {

	RenderController::RenderController(bool imguiEnabled)
		: window{ core::VulkanContext::getWindow() }
		, swapChain{ *core::VulkanContext::getSwapChain() }
		, device{ *core::VulkanContext::getDevice() }
		, renderManager{ std::make_unique<core::RenderManager>(device, swapChain, window, imguiEnabled) }
	{
	}

	void RenderController::render()
	{
		renderThread->getSynchronizer().endFrame();
	}

	void RenderController::beginFrame()
	{
		renderThread->getSynchronizer().beginFrame();
	}

	void RenderController::reSize()
	{
		renderThread->getSynchronizer().waitUntilIdle();

		device.getLogicalDevice().waitIdle();
		swapChain.recreate(window->getWidth(), window->getHeight());
		renderManager->recreate(window->getWidth(), window->getHeight());

		// Skip ImGui rendering for the next frame — the current frame's draw data
		// references descriptor sets that were just destroyed by the recreate.
		renderManager->skipImguiNextFrame();
	}

	RenderController::~RenderController() = default;

	void RenderController::init()
	{
		renderManager->init();

		renderThread = std::make_unique<core::RenderThread>();
		renderThread->start([this](uint32_t) { renderThreadCallback(); });
		vfLogInfo("RenderController: Render thread enabled");
	}

	void RenderController::renderThreadCallback()
	{
		using Clock = std::chrono::high_resolution_clock;

		static uint32_t renderThreadId = threading::TaskProfiler::instance().getMaxThreadId() + 1;
		auto baseTime = Clock::now();

		std::vector<threading::TaskProfileEntry> entries;

		auto profileBlock = [&](const char* name, auto&& fn) {
			auto t0 = Clock::now();
			fn();
			auto t1 = Clock::now();

			threading::TaskProfileEntry entry;
			entry.name = name;
			entry.threadId = renderThreadId;
			entry.startTimeNs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(t0 - baseTime).count());
			entry.endTimeNs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - baseTime).count());
			entries.push_back(entry);
		};

		if (preRenderCallback)
			profileBlock("OffScreenRender", preRenderCallback);

		profileBlock("SwapchainPresent", [this] { renderManager->render(); });

		threading::TaskProfiler::instance().appendToLatestFrame(entries);
	}

	void RenderController::cleanUp()
	{
		// Stop render thread BEFORE any Vulkan cleanup
		renderThread->stop();
		renderThread.reset();

		renderManager->cleanUp();
	}

	void RenderController::setResizeCallback(core::ResizeCallback callback)
	{
		renderManager->setResizeCallback(std::move(callback));
	}

	void RenderController::setBlitSourceProvider(core::BlitSourceProvider provider)
	{
		renderManager->setBlitSourceProvider(std::move(provider));
	}

	void RenderController::snapshotImGuiDrawData()
	{
		renderManager->snapshotImGuiDrawData();
	}

}
