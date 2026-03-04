#pragma once
#include "Device.hpp"
#include "SwapChain.hpp"
#include "print/Log.hpp"

#include <memory>

namespace window {
	class Window;
}

namespace core {
	class VulkanContext
	{
	private:
		inline static std::unique_ptr<Device> device;
		inline static std::unique_ptr<SwapChain> swapChain;
		inline static const window::Window* window;
	public:
		static void init(const window::Window* windowGlfw);
		static void cleanup();

		static std::unique_ptr<Device>& getDevice() {
			vfLogAssert(device == nullptr || device.get() == nullptr, "device is not initiated");
			return device; }

		static std::unique_ptr<SwapChain>& getSwapChain() {
			vfLogAssert(swapChain == nullptr || swapChain.get() == nullptr, "swapChain is not initiated");
			return swapChain; }

		static const window::Window* getWindow() {
			vfLogAssert(window == nullptr, "window is not initiated");
			return window;
		}

	};
}


