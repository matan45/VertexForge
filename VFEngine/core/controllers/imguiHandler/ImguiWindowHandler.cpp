#include "ImguiWindowHandler.hpp"
#include "../../../graphics/core/VulkanContext.hpp"
#include <ranges>
#include <algorithm>

namespace controllers::imguiHandler {
	void ImguiWindowHandler::add(const std::shared_ptr< ImguiWindow>& imguiWindow)
	{
		imguiWindows.push_back(imguiWindow);
	}

	void ImguiWindowHandler::remove(const std::shared_ptr<ImguiWindow>& imguiWindow)
	{
		auto new_end = std::ranges::remove(imguiWindows, imguiWindow);
		imguiWindows.erase(new_end.begin(), imguiWindows.end());
	}

	void ImguiWindowHandler::draw() {
		// Draw all windows (copy vector in case draw() adds new windows)
		auto windowsCopy = imguiWindows;
		for (const auto& window : windowsCopy) {
			if (window) {
				window->draw();
			}
		}

		// Check if any windows need to be closed
		bool hasClosingWindows = std::ranges::any_of(imguiWindows, [](const auto& window) {
			return window && window->shouldClose();
		});

		// Wait for GPU before destroying windows with potential Vulkan resources
		if (hasClosingWindows) {
			auto& device = core::VulkanContext::getDevice();
			if (device) {
				device->getLogicalDevice().waitIdle();
			}
		}

		// Remove closed windows
		std::erase_if(imguiWindows, [](const auto& window) {
			return window && window->shouldClose();
		});
	}

	void ImguiWindowHandler::cleanUp() {
		auto& device = core::VulkanContext::getDevice();
		if (device) {
			device->getLogicalDevice().waitIdle();
		}
		imguiWindows.clear();
	}
}



