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
		if (useRenderThread && renderThread)
		{
			// Signal the render thread that a new frame is ready
			renderThread->getSynchronizer().endFrame();
		}
		else
		{
			renderManager->render();
		}
	}

	void RenderController::beginFrame()
	{
		if (useRenderThread && renderThread)
		{
			renderThread->getSynchronizer().beginFrame();
		}
	}

	void RenderController::reSize()
	{
		if (useRenderThread && renderThread)
		{
			// Wait for the render thread to finish all pending frames
			renderThread->getSynchronizer().waitUntilIdle();
		}

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

		if (useRenderThread)
		{
			renderThread = std::make_unique<core::RenderThread>();
			renderThread->start([this](uint32_t /*frameSlot*/) {
				using Clock = std::chrono::high_resolution_clock;

				// Use a dedicated thread ID for the render thread (assigned once on first frame)
				static uint32_t renderThreadId = threading::TaskProfiler::instance().getMaxThreadId() + 1;

				std::vector<threading::TaskProfileEntry> entries;

				if (preRenderCallback)
				{
					auto t0 = Clock::now();
					preRenderCallback();
					auto t1 = Clock::now();

					threading::TaskProfileEntry entry;
					entry.name = "OffScreenRender";
					entry.threadId = renderThreadId;
					entry.startTimeNs = static_cast<uint64_t>(t0.time_since_epoch().count());
					entry.endTimeNs = static_cast<uint64_t>(t1.time_since_epoch().count());
					entries.push_back(entry);
				}

				{
					auto t0 = Clock::now();
					renderManager->render();
					auto t1 = Clock::now();

					threading::TaskProfileEntry entry;
					entry.name = "SwapchainPresent";
					entry.threadId = renderThreadId;
					entry.startTimeNs = static_cast<uint64_t>(t0.time_since_epoch().count());
					entry.endTimeNs = static_cast<uint64_t>(t1.time_since_epoch().count());
					entries.push_back(entry);
				}

				// Inject render thread entries into the profiler (append to current frame)
				threading::TaskProfiler::instance().appendToLatestFrame(entries);
			});
			vfLogInfo("RenderController: Render thread enabled");
		}
	}

	void RenderController::cleanUp()
	{
		// Stop render thread BEFORE any Vulkan cleanup
		if (renderThread)
		{
			renderThread->stop();
			renderThread.reset();
		}

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
