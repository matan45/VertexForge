
#include "Device.hpp"
#include "print/Logger.hpp"
#include "../window/Window.hpp"

#include <cassert>
#include <unordered_set>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE


static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData) {

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		loggerWarning("Validation layer warning: {}", pCallbackData->pMessage);
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		loggerError("Validation layer error: {}", pCallbackData->pMessage);
	}
	else {
		loggerInfo("Validation layer message: {}", pCallbackData->pMessage);
	}
	return VK_FALSE;
}


namespace core {

	Device::Device(const window::Window* window) : window{ window } {}

	void Device::init()
	{
		createInstance();
		createDebugMessenger();
		pickPhysicalDevice();
		createLogicalDevice();
	}

	void Device::cleanUp()
	{
		if (surface) {
			instance->destroySurfaceKHR(surface);
		}

		logicalDevice.reset();

		if (debug && debugMessenger) {
			instance->destroyDebugUtilsMessengerEXT(debugMessenger, nullptr, dldi);
		}
	}

	void Device::createInstance()
	{
		// Initialize the default dispatcher with vkGetInstanceProcAddr before any Vulkan calls
		VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

		vk::ApplicationInfo appInfo{
		   "Vulkan App",
		   VK_MAKE_VERSION(1, 0, 0),
		   "Engine",
		   VK_MAKE_VERSION(1, 0, 0),
		   VK_API_VERSION_1_3
		};

		std::vector<const char*> extensions = getRequiredExtensions();

		vk::InstanceCreateInfo createInfo{};
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();

		if (debug) {
			createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
			createInfo.ppEnabledLayerNames = validationLayers.data();
		}

		try {
			instance = vk::createInstanceUnique(createInfo);
			// Initialize dispatcher with instance for instance-level functions
			VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
		}
		catch (const vk::SystemError& err) {
			loggerError("Failed to create Vulkan instance: {}", err.what());
			throw;
		}

		if (debug) {
			for (const auto& extension : vk::enumerateInstanceExtensionProperties()) {
				loggerInfo("Available extension: {}", extension.extensionName);
			}
		}

		if (debug) {
			uint32_t version{ 0 };
			if (vk::Result result = vk::enumerateInstanceVersion(&version); result == vk::Result::eSuccess) {
				loggerInfo("Vulkan API version: {}.{}.{}",
					VK_API_VERSION_MAJOR(version),
					VK_API_VERSION_MINOR(version),
					VK_API_VERSION_PATCH(version));
			}
			else {
				loggerError("Failed to enumerate Vulkan instance version. Error code: {}", vk::to_string(result));
			}


			loggerInfo("Vulkan API version: {}.{}.{}",
				VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version), VK_API_VERSION_PATCH(version));

			if (!checkValidationLayerSupport()) {
				loggerError("Validation layers requested, but not available!");
			}
		}

		surface = window->createWindowSurface(instance);
	}

	std::vector<const char*> Device::getRequiredExtensions() const
	{
		uint32_t glfwExtensionCount = 0;
		const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

		if (debug) {
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		return extensions;
	}

	void Device::createDebugMessenger()
	{
		using enum vk::DebugUtilsMessageTypeFlagBitsEXT;
		if (!debug) return;

		dldi = vk::detail::DispatchLoaderDynamic(*instance, vkGetInstanceProcAddr);

		vk::DebugUtilsMessengerCreateInfoEXT createInfo{};
		createInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
		createInfo.messageType = eGeneral | eValidation | ePerformance;
		createInfo.pfnUserCallback = reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(debugCallback);

		try {
			debugMessenger = instance->createDebugUtilsMessengerEXT(createInfo, nullptr, dldi);
		}
		catch (const vk::SystemError& err) {
			loggerError("Failed to set up debug messenger: {}", err.what());
			throw;
		}
	}

	void Device::pickPhysicalDevice()
	{
		const std::vector<vk::PhysicalDevice> devices = instance->enumeratePhysicalDevices();

		if (debug) {
			loggerInfo("Found {} devices with Vulkan support.", devices.size());
		}

		for (const vk::PhysicalDevice& device : devices) {
			if (isDeviceSuitable(device)) {
				physicalDevice = device;
				if (debug) {
					loggerInfo("Selected physical device: {}", physicalDevice.getProperties().deviceName);
				}
				break;
			}
		}

		if (!physicalDevice) {
			loggerError("Failed to find a suitable GPU!");
		}

	}

	void Device::createLogicalDevice()
	{
		queueFamilyIndices = Utilities::findQueueFamiliesFromDevice(physicalDevice, surface);

		// Precondition: pickPhysicalDevice() ensures isComplete() was true
		assert(queueFamilyIndices.isComplete() &&
			"Queue families must be complete - was pickPhysicalDevice() called first?");

		const float queuePriority = 1.0f;

		std::unordered_set<uint32_t> uniqueQueueFamilies;
		uniqueQueueFamilies.insert(queueFamilyIndices.graphicsAndComputeFamily.value());
		uniqueQueueFamilies.insert(queueFamilyIndices.presentFamily.value());

		// Add dedicated transfer queue if available and different
		if (queueFamilyIndices.hasDedicatedTransferQueue()) {
			uniqueQueueFamilies.insert(queueFamilyIndices.transferFamily.value());
		}

		if (debug) {
			loggerInfo("Creating {} unique queue families:", uniqueQueueFamilies.size());
			loggerInfo("  Graphics/Compute family: {}", queueFamilyIndices.graphicsAndComputeFamily.value());
			loggerInfo("  Present family: {}", queueFamilyIndices.presentFamily.value());
			if (queueFamilyIndices.hasDedicatedTransferQueue()) {
				loggerInfo("  Dedicated Transfer family: {}", queueFamilyIndices.transferFamily.value());
			}
		}

		std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
		for (uint32_t queueFamily : uniqueQueueFamilies) {
			vk::DeviceQueueCreateInfo queueCreateInfo{};
			queueCreateInfo.queueFamilyIndex = queueFamily;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueCreateInfo);
			if (debug) {
				loggerInfo("  Creating queue for family {}", queueFamily);
			}
		}

		vk::PhysicalDeviceFeatures deviceFeatures{};
		deviceFeatures.samplerAnisotropy = VK_TRUE;

		vk::DeviceCreateInfo createInfo{};
		createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
		createInfo.ppEnabledExtensionNames = deviceExtensions.data();

		try {
			logicalDevice = physicalDevice.createDeviceUnique(createInfo);
			// Initialize dispatcher with device for device-level functions
			VULKAN_HPP_DEFAULT_DISPATCHER.init(*logicalDevice);
			graphicsAndComputeQueue = logicalDevice.get().getQueue(queueFamilyIndices.graphicsAndComputeFamily.value(), 0);
			presentQueue = logicalDevice.get().getQueue(queueFamilyIndices.presentFamily.value(), 0);

			// Get transfer queue (dedicated if available, otherwise use graphics queue)
			if (queueFamilyIndices.hasDedicatedTransferQueue()) {
				transferQueue = logicalDevice.get().getQueue(queueFamilyIndices.transferFamily.value(), 0);
				if (debug) {
					loggerInfo("Using dedicated transfer queue (family {}) for async transfers",
					           queueFamilyIndices.transferFamily.value());
				}
			} else {
				// Fall back to graphics queue for transfers
				transferQueue = graphicsAndComputeQueue;
				if (debug) {
					loggerInfo("Using graphics queue for transfers (no dedicated transfer queue available)");
				}
			}
		}
		catch (const vk::SystemError& err) {
			loggerError("Failed to create logical device: {}", err.what());
			throw;
		}
	}

	bool Device::checkValidationLayerSupport() const
	{
		if (uint32_t layerCount = 0; vk::enumerateInstanceLayerProperties(&layerCount, nullptr) == vk::Result::eSuccess) {
			std::vector<vk::LayerProperties> availableLayers(layerCount);
			if (vk::enumerateInstanceLayerProperties(&layerCount, availableLayers.data()) == vk::Result::eSuccess) {
				for (const auto& layer : availableLayers) {
					loggerInfo("Available validation layer: {}", layer.layerName);
				}
			}
			else {
				loggerError("Failed to retrieve Vulkan instance layers.");
			}
		}
		else {
			loggerError("Failed to count Vulkan instance layers.");
		}

		return true;
	}

	bool Device::isDeviceSuitable(const vk::PhysicalDevice& device) const
	{
		const QueueFamilyIndices indices = Utilities::findQueueFamiliesFromDevice(device, surface);

		const bool extensionsSupported = checkDeviceExtensionSupport(device);
		const vk::PhysicalDeviceFeatures supportedFeatures = device.getFeatures();
		const vk::PhysicalDeviceProperties deviceProperties = device.getProperties();

		return indices.isComplete() &&
			extensionsSupported &&
			supportedFeatures.samplerAnisotropy &&
			deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
	}

	bool Device::checkDeviceExtensionSupport(const vk::PhysicalDevice& device) const
	{
		// Convert the required extensions into an unordered set for fast lookup and erasure
		std::unordered_set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

		// Get available extensions for the current physical device
		std::vector<vk::ExtensionProperties> availableExtensions = device.enumerateDeviceExtensionProperties();

		if (debug) {
			loggerInfo("Found {} available device extensions.", availableExtensions.size());
		}

		// Remove each available extension from the required extensions set
		for (const vk::ExtensionProperties& extension : availableExtensions) {
			requiredExtensions.erase(extension.extensionName);

			// Optional debug logging to show which extensions are available
			if (debug) {
				loggerInfo("Available device extension: {}", extension.extensionName);
			}

			// Early exit if all required extensions have been found
			if (requiredExtensions.empty()) {
				return true;
			}
		}

		// If there are missing required extensions, log an error and return false
		if (!requiredExtensions.empty()) {
			if (debug) {
				for (const auto& ext : requiredExtensions) {
					loggerError("Required device extension not found: {}", ext);
				}
			}
			return false;
		}

		return true;
	}

}
