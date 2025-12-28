#pragma once

#include <vector>
#include <array>

#include "Utilities.hpp"

namespace window {
	class Window;
}

namespace core {

	// GPU memory information for resource allocation decisions
	struct DeviceMemoryInfo {
		vk::DeviceSize deviceLocalHeapSize = 0;      // Total device-local VRAM
		vk::DeviceSize hostVisibleHeapSize = 0;      // Host-visible memory
		bool hasUnifiedMemory = false;               // APU/integrated GPU
	};

	class Device
	{
	private:
		const window::Window* window;

		vk::UniqueInstance instance{ nullptr };
		vk::PhysicalDevice physicalDevice{ nullptr };
		vk::UniqueDevice logicalDevice{ nullptr };

		vk::DebugUtilsMessengerEXT debugMessenger{ nullptr };
		vk::detail::DispatchLoaderDynamic dldi;

		vk::SurfaceKHR surface{ nullptr };
		vk::Queue presentQueue{ nullptr };
		vk::Queue graphicsAndComputeQueue{ nullptr };
		vk::Queue transferQueue{ nullptr };

		QueueFamilyIndices queueFamilyIndices{};

		// Shared staging command pool for one-time transfer operations
		vk::UniqueCommandPool stagingCommandPool;

		const std::array<const char*, 1> validationLayers = { "VK_LAYER_KHRONOS_validation" };
		const std::array<const char*, 1> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		// Private functions for setup and initialization
		void createInstance();
		std::vector<const char*> getRequiredExtensions() const;
		void createDebugMessenger();
		void pickPhysicalDevice();
		void createLogicalDevice();
		void createStagingCommandPool();
		bool checkValidationLayerSupport() const;

		bool isDeviceSuitable(const vk::PhysicalDevice& device) const;
		bool checkDeviceExtensionSupport(const vk::PhysicalDevice& device) const;

	public:
		explicit Device(const window::Window* window);
		~Device() = default;

		void init();
		void cleanUp();

		const vk::SurfaceKHR& getSurface() const { return surface; }
		const vk::Instance& getInstance() const { return instance.get(); }
		const vk::PhysicalDevice& getPhysicalDevice() const { return physicalDevice; }
		const vk::Device& getLogicalDevice() const { return logicalDevice.get(); }
		const QueueFamilyIndices& getQueueFamilyIndices() const { return queueFamilyIndices; }
		const vk::Queue& getPresentQueue() const { return presentQueue; }
		const vk::Queue& getGraphicsQueue() const { return graphicsAndComputeQueue; }
		const vk::Queue& getTransferQueue() const { return transferQueue; }
		bool hasDedicatedTransferQueue() const { return queueFamilyIndices.hasDedicatedTransferQueue(); }

		// Shared staging command pool for one-time transfer operations (texture uploads, buffer copies)
		const vk::CommandPool& getStagingCommandPool() const { return stagingCommandPool.get(); }

		// Query device memory information for resource allocation decisions
		DeviceMemoryInfo getDeviceMemoryInfo() const;

	};

}