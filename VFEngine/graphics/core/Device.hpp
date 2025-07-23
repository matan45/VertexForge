
#include <vector>
#include <array>

#include "Utilities.hpp"

namespace window {
	class Window;
}

namespace core {
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

		QueueFamilyIndices queueFamilyIndices{};

		const std::array<const char*, 1> validationLayers = { "VK_LAYER_KHRONOS_validation" };
		const std::array<const char*, 1> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		// Private functions for setup and initialization
		void createInstance();
		std::vector<const char*> getRequiredExtensions() const;
		void createDebugMessenger();
		void pickPhysicalDevice();
		void createLogicalDevice();
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

	};

}