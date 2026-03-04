
#include "Device.hpp"
#include "print/Log.hpp"
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
		vfLogWarning("Validation layer warning: {}", pCallbackData->pMessage);
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		vfLogError("Validation layer error: {}", pCallbackData->pMessage);
	}
	else {
		vfLogInfo("Validation layer message: {}", pCallbackData->pMessage);
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
		queryMeshShaderCapabilities();
		createStagingCommandPool();
	}

	void Device::cleanUp()
	{
		// Reset staging command pool before device
		stagingCommandPool.reset();

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
			vfLogError("Failed to create Vulkan instance: {}", err.what());
			throw;
		}

		if (debug) {
			uint32_t version{ 0 };
			if (vk::Result result = vk::enumerateInstanceVersion(&version); result == vk::Result::eSuccess) {
				vfLogInfo("Vulkan API version: {}.{}.{}",
					VK_API_VERSION_MAJOR(version),
					VK_API_VERSION_MINOR(version),
					VK_API_VERSION_PATCH(version));
			}
			else {
				vfLogError("Failed to enumerate Vulkan instance version. Error code: {}", vk::to_string(result));
			}

			if (!checkValidationLayerSupport()) {
				vfLogError("Validation layers requested, but not available!");
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
			vfLogError("Failed to set up debug messenger: {}", err.what());
			throw;
		}
	}

	void Device::pickPhysicalDevice()
	{
		const std::vector<vk::PhysicalDevice> devices = instance->enumeratePhysicalDevices();

		if (debug) {
			vfLogInfo("Found {} devices with Vulkan support.", devices.size());
		}

		for (const vk::PhysicalDevice& device : devices) {
			if (isDeviceSuitable(device)) {
				physicalDevice = device;
				if (debug) {
					vfLogInfo("Selected physical device: {}", static_cast<const char*>(physicalDevice.getProperties().deviceName));
				}
				break;
			}
		}

		if (!physicalDevice) {
			vfLogError("Failed to find a suitable GPU!");
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

		std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
		for (uint32_t queueFamily : uniqueQueueFamilies) {
			vk::DeviceQueueCreateInfo queueCreateInfo{};
			queueCreateInfo.queueFamilyIndex = queueFamily;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueCreateInfo);
		}

		vk::PhysicalDeviceFeatures deviceFeatures{};
		deviceFeatures.samplerAnisotropy = VK_TRUE;
		deviceFeatures.independentBlend = VK_TRUE;

		// required for gl_BaseInstance in shaders
		vk::PhysicalDeviceVulkan11Features vulkan11Features{};
		vulkan11Features.shaderDrawParameters = VK_TRUE;

		// Vulkan 1.2 features (required for drawIndirectCount and descriptor indexing)
		vk::PhysicalDeviceVulkan12Features vulkan12Features{};
		vulkan12Features.drawIndirectCount = VK_TRUE;
		vulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
		vulkan12Features.runtimeDescriptorArray = VK_TRUE;
		vulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;
		vulkan12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;
		vulkan12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
		vulkan12Features.pNext = &vulkan11Features; 

		vk::PhysicalDeviceVulkan13Features vulkan13Features{};
		vulkan13Features.shaderDemoteToHelperInvocation = VK_TRUE;
		vulkan13Features.maintenance4 = VK_TRUE;  // Required for mesh shader LocalSizeId
		vulkan13Features.pNext = &vulkan12Features;  

		// Mesh shader features (VK_EXT_mesh_shader)
		vk::PhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
		meshShaderFeatures.taskShader = VK_TRUE;
		meshShaderFeatures.meshShader = VK_TRUE;
		meshShaderFeatures.pNext = &vulkan13Features;

		vk::DeviceCreateInfo createInfo{};
		createInfo.pNext = &meshShaderFeatures;
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
			} else {
				transferQueue = graphicsAndComputeQueue;
			}
		}
		catch (const vk::SystemError& err) {
			vfLogError("Failed to create logical device: {}", err.what());
			throw;
		}
	}

	void Device::createStagingCommandPool()
	{
		vk::CommandPoolCreateInfo poolInfo{};
		poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
		poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsAndComputeFamily.value();

		try {
			stagingCommandPool = logicalDevice->createCommandPoolUnique(poolInfo);
		}
		catch (const vk::SystemError& err) {
			vfLogError("Failed to create staging command pool: {}", err.what());
			throw;
		}
	}

	bool Device::checkValidationLayerSupport() const
	{
		if (uint32_t layerCount = 0; vk::enumerateInstanceLayerProperties(&layerCount, nullptr) == vk::Result::eSuccess) {
			std::vector<vk::LayerProperties> availableLayers(layerCount);
			if (vk::enumerateInstanceLayerProperties(&layerCount, availableLayers.data()) != vk::Result::eSuccess) {
				vfLogError("Failed to retrieve Vulkan instance layers.");
				return false;
			}
		}
		else {
			vfLogError("Failed to count Vulkan instance layers.");
			return false;
		}

		return true;
	}

	bool Device::isDeviceSuitable(const vk::PhysicalDevice& device) const
	{
		const QueueFamilyIndices indices = Utilities::findQueueFamiliesFromDevice(device, surface);
		const bool extensionsSupported = checkDeviceExtensionSupport(device);
		const vk::PhysicalDeviceFeatures supportedFeatures = device.getFeatures();
		const vk::PhysicalDeviceProperties deviceProperties = device.getProperties();
		
		if (debug) {
			const std::string deviceName = deviceProperties.deviceName;
			if (!indices.isComplete()) {
				vfLogInfo("Device '{}' rejected: incomplete queue families", deviceName);
			}
			if (!extensionsSupported) {
				vfLogInfo("Device '{}' rejected: missing required extensions (including VK_EXT_mesh_shader)", deviceName);
			}
			if (!supportedFeatures.samplerAnisotropy) {
				vfLogInfo("Device '{}' rejected: no sampler anisotropy support", deviceName);
			}
			if (deviceProperties.deviceType != vk::PhysicalDeviceType::eDiscreteGpu) {
				vfLogInfo("Device '{}' rejected: not a discrete GPU (type: {})",
				           deviceName, vk::to_string(deviceProperties.deviceType));
			}
		}

		return indices.isComplete() &&
			extensionsSupported &&
			supportedFeatures.samplerAnisotropy &&
			deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
	}

	bool Device::checkDeviceExtensionSupport(const vk::PhysicalDevice& device) const
	{
		std::unordered_set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
		
		std::vector<vk::ExtensionProperties> availableExtensions = device.enumerateDeviceExtensionProperties();

		for (const vk::ExtensionProperties& extension : availableExtensions) {
			requiredExtensions.erase(extension.extensionName);

			if (requiredExtensions.empty()) {
				return true;
			}
		}
		
		if (!requiredExtensions.empty()) {
			if (debug) {
				for (const auto& ext : requiredExtensions) {
					vfLogInfo("Device extension not available: {}", ext);
				}
			}
			return false;
		}

		return true;
	}

	void Device::queryMeshShaderCapabilities()
	{
		bool meshShaderExtensionFound = false;
		for (const auto& ext : physicalDevice.enumerateDeviceExtensionProperties()) {
			if (strcmp(ext.extensionName.data(), VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0) {
				meshShaderExtensionFound = true;
				break;
			}
		}

		if (!meshShaderExtensionFound) {
			vfLogWarning("VK_EXT_mesh_shader not available - mesh shader capabilities will be zero");
			return;
		}
		
		vk::PhysicalDeviceMeshShaderPropertiesEXT meshProps{};
		vk::PhysicalDeviceProperties2 props2{};
		props2.pNext = &meshProps;
		physicalDevice.getProperties2(&props2);
		
		vk::PhysicalDeviceMeshShaderFeaturesEXT meshFeatures{};
		vk::PhysicalDeviceFeatures2 features2{};
		features2.pNext = &meshFeatures;
		physicalDevice.getFeatures2(&features2);
		
		meshShaderCapabilities.meshShaderSupported = meshFeatures.meshShader;
		meshShaderCapabilities.taskShaderSupported = meshFeatures.taskShader;
		meshShaderCapabilities.meshShaderQueriesSupported = meshFeatures.meshShaderQueries;
		
		meshShaderCapabilities.maxMeshOutputVertices = meshProps.maxMeshOutputVertices;
		meshShaderCapabilities.maxMeshOutputPrimitives = meshProps.maxMeshOutputPrimitives;
		meshShaderCapabilities.maxMeshWorkGroupInvocations = meshProps.maxMeshWorkGroupInvocations;
		meshShaderCapabilities.maxMeshWorkGroupSize = {
			meshProps.maxMeshWorkGroupSize[0],
			meshProps.maxMeshWorkGroupSize[1],
			meshProps.maxMeshWorkGroupSize[2]
		};
		meshShaderCapabilities.maxMeshOutputMemorySize = meshProps.maxMeshOutputMemorySize;
		meshShaderCapabilities.maxMeshPayloadAndOutputMemorySize = meshProps.maxMeshPayloadAndOutputMemorySize;
		
		meshShaderCapabilities.maxTaskWorkGroupInvocations = meshProps.maxTaskWorkGroupInvocations;
		meshShaderCapabilities.maxTaskWorkGroupSize = {
			meshProps.maxTaskWorkGroupSize[0],
			meshProps.maxTaskWorkGroupSize[1],
			meshProps.maxTaskWorkGroupSize[2]
		};
		meshShaderCapabilities.maxTaskPayloadSize = meshProps.maxTaskPayloadSize;
		
		meshShaderCapabilities.maxPreferredMeshWorkGroupInvocations = meshProps.maxPreferredMeshWorkGroupInvocations;
		meshShaderCapabilities.maxPreferredTaskWorkGroupInvocations = meshProps.maxPreferredTaskWorkGroupInvocations;

		if (debug) {
			vfLogInfo("Mesh shader: mesh={}, task={}, maxVerts={}, maxPrims={}",
				meshShaderCapabilities.meshShaderSupported,
				meshShaderCapabilities.taskShaderSupported,
				meshShaderCapabilities.maxMeshOutputVertices,
				meshShaderCapabilities.maxMeshOutputPrimitives);
		}
	}

	DeviceMemoryInfo Device::getDeviceMemoryInfo() const
	{
		DeviceMemoryInfo info{};

		vk::PhysicalDeviceMemoryProperties memProps = physicalDevice.getMemoryProperties();

		// Find the largest device-local and host-visible heaps
		for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
			const auto& heap = memProps.memoryHeaps[i];

			if (heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
				if (heap.size > info.deviceLocalHeapSize) {
					info.deviceLocalHeapSize = heap.size;
				}
			}
		}

		// Check memory types to find host-visible memory and detect unified memory
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
			const auto& memType = memProps.memoryTypes[i];
			const auto& heap = memProps.memoryHeaps[memType.heapIndex];

			// Host-visible memory
			if (memType.propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible) {
				if (heap.size > info.hostVisibleHeapSize) {
					info.hostVisibleHeapSize = heap.size;
				}

				// Unified memory: device-local AND host-visible in same type
				if (memType.propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal) {
					info.hasUnifiedMemory = true;
				}
			}
		}

		return info;
	}

}
