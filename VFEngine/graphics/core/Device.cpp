#include "Device.hpp"
#include "VulkanMemoryManager.hpp"
#include "print/Log.hpp"
#include "../window/Window.hpp"
#include "../render/upscaling/UpscaleManager.hpp"

#include <cassert>
#include <fstream>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

// When Streamline is enabled, route all Vulkan calls through the interposer
// so that Streamline can track resources for DLSS/Reflex.
static PFN_vkGetInstanceProcAddr getVulkanProcAddr()
{
#ifdef VF_STREAMLINE_ENABLED
    HMODULE slModule = GetModuleHandleW(L"sl.interposer.dll");
    if (slModule)
    {
        auto proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            GetProcAddress(slModule, "vkGetInstanceProcAddr"));
        if (proc)
        {
            vfLogInfo("Using Streamline interposer vkGetInstanceProcAddr for Vulkan dispatch");
            return proc;
        }
    }
    vfLogWarning("sl.interposer.dll not found, falling back to native vkGetInstanceProcAddr");
#endif
    return vkGetInstanceProcAddr;
}


static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    // Suppress known harmless Streamline SDK interposer validation artifacts.
    // The interposer wraps Vulkan handles (descriptor sets, images) and manages
    // internal resource copies (sl.tag.*) that the validation layer doesn't understand.
    if (pCallbackData->pMessageIdName)
    {
        std::string_view vuid(pCallbackData->pMessageIdName);
        if (vuid == "VUID-VkImageViewCreateInfo-usage-02275" ||
            vuid == "VUID-VkImageViewCreateInfo-image-01762" ||
            vuid == "VUID-VkDeviceCreateInfo-pNext-04748" ||
            vuid == "VUID-vkCmdBindDescriptorSets-pDescriptorSets-parameter" ||
            vuid == "VUID-vkCmdBindDescriptorSets-pDescriptorSets-06563" ||
            vuid == "VUID-vkCmdDrawIndexed-None-08600" ||
            vuid == "VUID-vkCmdDraw-None-09600" ||
            vuid == "VUID-VkImageMemoryBarrier-oldLayout-01197" ||
            vuid == "VUID-VkImageMemoryBarrier-image-03320" ||
            vuid == "VUID-vkDestroyDevice-device-05137")
        {
            return VK_FALSE;
        }
    }
    // Suppress Streamline object tracking messages (proxy handles, internal resource copies)
    if (pCallbackData->pMessage)
    {
        std::string_view msg(pCallbackData->pMessage);
        if (msg.find("Couldn't find VkDescriptorSet Object") != std::string_view::npos ||
            msg.find("sl.tag.") != std::string_view::npos ||
            msg.find("sl.dlssg.fake-swapchain-buffer") != std::string_view::npos ||
            msg.find("SL_present_semaphore") != std::string_view::npos ||
            msg.find("Object Tracking") != std::string_view::npos)
        {
            return VK_FALSE;
        }
    }

    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        vfLogWarning("Validation layer warning: {}", pCallbackData->pMessage);
    }
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        vfLogError("Validation layer error: {}", pCallbackData->pMessage);
    }
    else
    {
        vfLogInfo("Validation layer message: {}", pCallbackData->pMessage);
    }
    return VK_FALSE;
}


namespace core
{
    Device::Device(const window::Window* window) : window{window}
    {
    }

    Device::~Device() = default;

    void Device::init()
    {
        // Initialize Streamline SDK BEFORE Vulkan calls so it can intercept
        // vkCreateInstance/vkCreateDevice for DLSS/Reflex setup.
        render::upscaling::UpscaleManager::initStreamline();

        createInstance();
        createDebugMessenger();
        pickPhysicalDevice();
        createLogicalDevice();
        queryMeshShaderCapabilities();
        queryRayQueryCapabilities();
        createStagingCommandPool();
        createPipelineCache();

        memoryManager = std::make_unique<VulkanMemoryManager>(*this);

        // Provide Vulkan device info to Streamline after device is fully created
        if (render::upscaling::UpscaleManager::isStreamlineAvailable())
        {
            upscaleManager = std::make_unique<render::upscaling::UpscaleManager>();
            upscaleManager->setVulkanDevice(*this);
        }
    }

    void Device::cleanUp()
    {
        savePipelineCacheToDisk();
        pipelineCache.reset();

        // Reset staging command pool before device
        stagingCommandPool.reset();

        // Wait for GPU to finish all work before Streamline cleanup
        if (logicalDevice)
            logicalDevice.get().waitIdle();

        // Shut down Streamline before destroying memory manager and device
        if (upscaleManager)
        {
            upscaleManager->shutdown();
            upscaleManager.reset();
        }

        // Destroy memory manager after all subsystems have cleaned up their buffers,
        // but before the logical device is destroyed
        memoryManager.reset();

        if (surface)
        {
            instance->destroySurfaceKHR(surface);
        }

        logicalDevice.reset();

        if (debug && debugMessenger)
        {
            instance->destroyDebugUtilsMessengerEXT(debugMessenger, nullptr, dldi);
        }
    }

    void Device::createInstance()
    {
        // Use Streamline interposer's vkGetInstanceProcAddr (if available) so that
        // Streamline can track all Vulkan resources for DLSS/Reflex.
        vulkanProcAddr = getVulkanProcAddr();
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vulkanProcAddr);

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

        if (debug)
        {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        }

        try
        {
            instance = vk::createInstanceUnique(createInfo);
            // Initialize dispatcher with instance for instance-level functions
            VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
        }
        catch (const vk::SystemError& err)
        {
            vfLogError("Failed to create Vulkan instance: {}", err.what());
            throw;
        }


        uint32_t version{0};
        if (vk::Result result = vk::enumerateInstanceVersion(&version); result == vk::Result::eSuccess)
        {
            vfLogInfo("Vulkan API version: {}.{}.{}",
                      VK_API_VERSION_MAJOR(version),
                      VK_API_VERSION_MINOR(version),
                      VK_API_VERSION_PATCH(version));
        }
        else
        {
            vfLogError("Failed to enumerate Vulkan instance version. Error code: {}", vk::to_string(result));
        }

        if (!checkValidationLayerSupport())
        {
            vfLogError("Validation layers requested, but not available!");
        }
        
        if (window)
        {
            surface = window->createWindowSurface(instance);
        }
    }

    std::vector<const char*> Device::getRequiredExtensions() const
    {
        std::vector<const char*> extensions;

        if (window)
        {
            uint32_t glfwExtensionCount = 0;
            const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
            extensions.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);
        }

        if (debug)
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        // Add Streamline SDK required instance extensions (for DLSS)
        auto slExtensions = render::upscaling::UpscaleManager::getRequiredInstanceExtensions();
        extensions.insert(extensions.end(), slExtensions.begin(), slExtensions.end());

        return extensions;
    }

    void Device::createDebugMessenger()
    {
        using enum vk::DebugUtilsMessageTypeFlagBitsEXT;
        if (!debug) return;

        dldi = vk::detail::DispatchLoaderDynamic(*instance, vulkanProcAddr);

        vk::DebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        createInfo.messageType = eGeneral | eValidation | ePerformance;
        createInfo.pfnUserCallback = reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(debugCallback);

        try
        {
            debugMessenger = instance->createDebugUtilsMessengerEXT(createInfo, nullptr, dldi);
        }
        catch (const vk::SystemError& err)
        {
            vfLogError("Failed to set up debug messenger: {}", err.what());
            throw;
        }
    }

    void Device::pickPhysicalDevice()
    {
        const std::vector<vk::PhysicalDevice> devices = instance->enumeratePhysicalDevices();

        if (debug)
        {
            vfLogDebug("Found {} devices with Vulkan support.", devices.size());
        }

        for (const vk::PhysicalDevice& device : devices)
        {
            if (isDeviceSuitable(device))
            {
                physicalDevice = device;
                if (debug)
                {
                    vfLogInfo("Selected physical device: {}",
                              static_cast<const char*>(physicalDevice.getProperties().deviceName));

                    auto extensions = physicalDevice.enumerateDeviceExtensionProperties();
                    for (const auto* req : deviceExtensions)
                    {
                        bool found = false;
                        for (const auto& ext : extensions)
                        {
                            if (strcmp(ext.extensionName.data(), req) == 0) { found = true; break; }
                        }
                        vfLogInfo("  Extension {}: {}", req, found ? "supported" : "NOT supported");
                    }
                }
                break;
            }
        }

        if (!physicalDevice)
        {
            vfLogError("Failed to find a suitable GPU!");
        }
    }

    void Device::createLogicalDevice()
    {
        queueFamilyIndices = Utilities::findQueueFamiliesFromDevice(physicalDevice, surface);

        // Precondition: pickPhysicalDevice() ensures queue families are available
        assert((window ? queueFamilyIndices.isComplete() : queueFamilyIndices.isCompleteHeadless()) &&
            "Queue families must be complete - was pickPhysicalDevice() called first?");

        // Queue priorities: graphics=1.0, async compute=0.5
        const std::array<float, 2> queuePriorities = {1.0f, 0.5f};

        std::unordered_set<uint32_t> uniqueQueueFamilies;
        uniqueQueueFamilies.insert(queueFamilyIndices.graphicsAndComputeFamily.value());
        if (queueFamilyIndices.presentFamily.has_value())
        {
            uniqueQueueFamilies.insert(queueFamilyIndices.presentFamily.value());
        }

        // Add dedicated transfer queue if available and different
        if (queueFamilyIndices.hasDedicatedTransferQueue())
        {
            uniqueQueueFamilies.insert(queueFamilyIndices.transferFamily.value());
        }

        // Add dedicated async compute family if it's a different family
        if (queueFamilyIndices.hasDedicatedComputeFamily())
        {
            uniqueQueueFamilies.insert(queueFamilyIndices.asyncComputeFamily.value());
        }

        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            vk::DeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.queueFamilyIndex = queueFamily;

            // Request 2 queues from graphics+compute family when using second queue for async compute
            if (queueFamily == queueFamilyIndices.graphicsAndComputeFamily.value() &&
                queueFamilyIndices.asyncComputeUsesSecondQueue)
            {
                queueCreateInfo.queueCount = 2;
                queueCreateInfo.pQueuePriorities = queuePriorities.data();
            }
            else
            {
                queueCreateInfo.queueCount = 1;
                queueCreateInfo.pQueuePriorities = queuePriorities.data(); // Uses first element (1.0f)
            }
            queueCreateInfos.push_back(queueCreateInfo);
        }

        vk::PhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        deviceFeatures.independentBlend = VK_TRUE;
        deviceFeatures.textureCompressionBC = VK_TRUE;

        vk::PhysicalDeviceFeatures supportedFeatures = physicalDevice.getFeatures();
        if (supportedFeatures.fillModeNonSolid)
        {
            deviceFeatures.fillModeNonSolid = VK_TRUE;
        }

        // required for gl_BaseInstance in shaders
        vk::PhysicalDeviceVulkan11Features vulkan11Features{};
        vulkan11Features.shaderDrawParameters = VK_TRUE;

        // Vulkan 1.2 features (required for drawIndirectCount, descriptor indexing, buffer device address)
        vk::PhysicalDeviceVulkan12Features vulkan12Features{};
        vulkan12Features.drawIndirectCount = VK_TRUE;
        vulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        vulkan12Features.runtimeDescriptorArray = VK_TRUE;
        vulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;
        vulkan12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;
        vulkan12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        vulkan12Features.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        vulkan12Features.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
        // Required for acceleration structures.
        // Note: Streamline injects VK_EXT_buffer_device_address which technically conflicts
        // with the Vulkan 1.2 core feature. The validation warning is suppressed in debugCallback
        // (VUID-VkDeviceCreateInfo-pNext-04748) — both paths enable the same functionality.
        vulkan12Features.bufferDeviceAddress = VK_TRUE;
        vulkan12Features.timelineSemaphore = VK_TRUE; // Required for async compute synchronization
        vulkan12Features.pNext = &vulkan11Features;

        vk::PhysicalDeviceVulkan13Features vulkan13Features{};
        vulkan13Features.synchronization2 = VK_TRUE; // Required for pipelineBarrier2KHR (render graph + depth copy)
        vulkan13Features.dynamicRendering = VK_TRUE; // Required for vkCmdBeginRendering (render graph migration)
        vulkan13Features.shaderDemoteToHelperInvocation = VK_TRUE;
        vulkan13Features.maintenance4 = VK_TRUE; // Required for mesh shader LocalSizeId
        vulkan13Features.pNext = &vulkan12Features;

        // Ray query features (VK_KHR_ray_query)
        vk::PhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
        rayQueryFeatures.rayQuery = VK_TRUE;
        rayQueryFeatures.pNext = &vulkan13Features;

        // Acceleration structure features (VK_KHR_acceleration_structure)
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelStructFeatures{};
        accelStructFeatures.accelerationStructure = VK_TRUE;
        accelStructFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;
        accelStructFeatures.pNext = &rayQueryFeatures;

        // Mesh shader features (VK_EXT_mesh_shader)
        vk::PhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
        meshShaderFeatures.taskShader = VK_TRUE;
        meshShaderFeatures.meshShader = VK_TRUE;
        meshShaderFeatures.pNext = &accelStructFeatures;

        // Build active extension list — skip swapchain in headless mode
        std::vector<const char*> activeDeviceExtensions;
        for (const auto* ext : deviceExtensions)
        {
            if (!window && strcmp(ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                continue;
            activeDeviceExtensions.push_back(ext);
        }

        // Add Streamline SDK required device extensions (for DLSS)
        auto slDeviceExtensions = render::upscaling::UpscaleManager::getRequiredDeviceExtensions();
        activeDeviceExtensions.insert(activeDeviceExtensions.end(),
                                      slDeviceExtensions.begin(), slDeviceExtensions.end());

        vk::DeviceCreateInfo createInfo{};
        createInfo.pNext = &meshShaderFeatures;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(activeDeviceExtensions.size());
        createInfo.ppEnabledExtensionNames = activeDeviceExtensions.data();

        try
        {
            logicalDevice = physicalDevice.createDeviceUnique(createInfo);
            // Initialize dispatcher with device for device-level functions
            VULKAN_HPP_DEFAULT_DISPATCHER.init(*logicalDevice);
            graphicsAndComputeQueue = logicalDevice.get().getQueue(queueFamilyIndices.graphicsAndComputeFamily.value(),
                                                                   0);
            if (queueFamilyIndices.presentFamily.has_value())
            {
                presentQueue = logicalDevice.get().getQueue(queueFamilyIndices.presentFamily.value(), 0);
            }

            // Get transfer queue (dedicated if available, otherwise use graphics queue)
            if (queueFamilyIndices.hasDedicatedTransferQueue())
            {
                transferQueue = logicalDevice.get().getQueue(queueFamilyIndices.transferFamily.value(), 0);
            }
            else
            {
                transferQueue = graphicsAndComputeQueue;
            }

            // Get async compute queue
            if (queueFamilyIndices.hasAsyncComputeQueue())
            {
                if (queueFamilyIndices.asyncComputeUsesSecondQueue)
                {
                    // Second queue from the same graphics+compute family
                    asyncComputeQueue = logicalDevice.get().getQueue(
                        queueFamilyIndices.graphicsAndComputeFamily.value(), 1);
                }
                else
                {
                    // Dedicated compute-only family
                    asyncComputeQueue = logicalDevice.get().getQueue(
                        queueFamilyIndices.asyncComputeFamily.value(), 0);
                }

                if (debug)
                {
                    vfLogDebug("Async compute queue enabled (family={}, dedicated={})",
                              queueFamilyIndices.asyncComputeFamily.value(),
                              queueFamilyIndices.hasDedicatedComputeFamily());
                }
            }
        }
        catch (const vk::SystemError& err)
        {
            vfLogError("Failed to create logical device: {}", err.what());
            throw;
        }
    }

    void Device::createStagingCommandPool()
    {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsAndComputeFamily.value();

        try
        {
            stagingCommandPool = logicalDevice->createCommandPoolUnique(poolInfo);
        }
        catch (const vk::SystemError& err)
        {
            vfLogError("Failed to create staging command pool: {}", err.what());
            throw;
        }
    }

    void Device::createPipelineCache()
    {
        namespace fs = std::filesystem;

        fs::path cacheDir = fs::current_path() / "cache";
        std::error_code ec;
        fs::create_directories(cacheDir, ec);
        if (ec)
        {
            vfLogWarning("Failed to create pipeline cache directory '{}': {}", cacheDir.string(), ec.message());
        }
        pipelineCachePath = cacheDir / "pipeline_cache.bin";

        loadPipelineCacheFromDisk();

        if (!pipelineCache)
        {
            vk::PipelineCacheCreateInfo cacheInfo{};
            try
            {
                pipelineCache = logicalDevice->createPipelineCacheUnique(cacheInfo);
                vfLogDebug("Pipeline cache created (empty)");
            }
            catch (const vk::SystemError& err)
            {
                vfLogWarning("Failed to create pipeline cache: {}", err.what());
            }
        }
    }

    void Device::loadPipelineCacheFromDisk()
    {
        if (!std::filesystem::exists(pipelineCachePath))
        {
            return;
        }

        std::ifstream file(pipelineCachePath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            return;
        }

        auto fileSize = file.tellg();
        if (fileSize <= 0)
        {
            return;
        }

        std::vector<uint8_t> cacheData(static_cast<size_t>(fileSize));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(cacheData.data()), fileSize);
        file.close();

        vk::PipelineCacheCreateInfo cacheInfo{};
        cacheInfo.initialDataSize = cacheData.size();
        cacheInfo.pInitialData = cacheData.data();

        try
        {
            pipelineCache = logicalDevice->createPipelineCacheUnique(cacheInfo);
            vfLogDebug("Pipeline cache loaded from disk ({} bytes)", cacheData.size());
        }
        catch (const vk::SystemError& err)
        {
            vfLogWarning("Failed to load pipeline cache from disk: {}", err.what());
        }
    }

    void Device::savePipelineCacheToDisk() const
    {
        if (!pipelineCache || pipelineCachePath.empty())
        {
            return;
        }

        try
        {
            auto cacheData = logicalDevice->getPipelineCacheData(pipelineCache.get());

            std::ofstream file(pipelineCachePath, std::ios::binary);
            if (file.is_open())
            {
                file.write(reinterpret_cast<const char*>(cacheData.data()),
                           static_cast<std::streamsize>(cacheData.size()));
                vfLogDebug("Pipeline cache saved to disk ({} bytes)", cacheData.size());
            }
        }
        catch (const vk::SystemError& err)
        {
            vfLogWarning("Failed to save pipeline cache: {}", err.what());
        }
    }

    bool Device::checkValidationLayerSupport() const
    {
        if (uint32_t layerCount = 0; vk::enumerateInstanceLayerProperties(&layerCount, nullptr) == vk::Result::eSuccess)
        {
            std::vector<vk::LayerProperties> availableLayers(layerCount);
            if (vk::enumerateInstanceLayerProperties(&layerCount, availableLayers.data()) != vk::Result::eSuccess)
            {
                vfLogError("Failed to retrieve Vulkan instance layers.");
                return false;
            }
        }
        else
        {
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

        if (!supportedFeatures.textureCompressionBC)
        {
            vfLogWarning("Device {} does not support BC texture compression",
                         static_cast<const char*>(deviceProperties.deviceName));
        }

        bool queueComplete = window ? indices.isComplete() : indices.isCompleteHeadless();

        // In headless mode, accept any GPU type (integrated, software) for CI/VM compatibility
        bool gpuTypeOk = window
            ? deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu
            : (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu ||
               deviceProperties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu ||
               deviceProperties.deviceType == vk::PhysicalDeviceType::eVirtualGpu ||
               deviceProperties.deviceType == vk::PhysicalDeviceType::eCpu);

        return queueComplete &&
            extensionsSupported &&
            supportedFeatures.samplerAnisotropy &&
            gpuTypeOk;
    }

    bool Device::checkDeviceExtensionSupport(const vk::PhysicalDevice& device) const
    {
        std::unordered_set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

        // Skip swapchain requirement in headless mode
        if (!window)
        {
            requiredExtensions.erase(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }

        std::vector<vk::ExtensionProperties> availableExtensions = device.enumerateDeviceExtensionProperties();

        for (const vk::ExtensionProperties& extension : availableExtensions)
        {
            requiredExtensions.erase(extension.extensionName);

            if (requiredExtensions.empty())
            {
                return true;
            }
        }

        if (!requiredExtensions.empty())
        {
            if (debug)
            {
                std::string deviceName = static_cast<const char*>(device.getProperties().deviceName);
                for (const auto& ext : requiredExtensions)
                {
                    vfLogDebug("Skipping device '{}': missing extension {}", deviceName, ext);
                }
            }
            return false;
        }

        return true;
    }

    void Device::queryMeshShaderCapabilities()
    {
        bool meshShaderExtensionFound = false;
        for (const auto& ext : physicalDevice.enumerateDeviceExtensionProperties())
        {
            if (strcmp(ext.extensionName.data(), VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0)
            {
                meshShaderExtensionFound = true;
                break;
            }
        }

        if (!meshShaderExtensionFound)
        {
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
    }

    void Device::queryRayQueryCapabilities()
    {
        // Check ray query extension
        bool rayQueryFound = false;
        bool accelStructFound = false;
        for (const auto& ext : physicalDevice.enumerateDeviceExtensionProperties())
        {
            if (strcmp(ext.extensionName.data(), VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0)
                rayQueryFound = true;
            if (strcmp(ext.extensionName.data(), VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
                accelStructFound = true;
        }

        if (!rayQueryFound || !accelStructFound)
        {
            vfLogWarning("VK_KHR_ray_query or VK_KHR_acceleration_structure not available");
            return;
        }

        // Query features
        vk::PhysicalDeviceRayQueryFeaturesKHR rqFeatures{};
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
        asFeatures.pNext = &rqFeatures;
        vk::PhysicalDeviceFeatures2 features2{};
        features2.pNext = &asFeatures;
        physicalDevice.getFeatures2(&features2);

        rayQueryCapabilities.rayQuerySupported = rqFeatures.rayQuery;
        rayQueryCapabilities.accelerationStructureSupported = asFeatures.accelerationStructure;

        // Query properties
        vk::PhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
        vk::PhysicalDeviceProperties2 props2{};
        props2.pNext = &asProps;
        physicalDevice.getProperties2(&props2);

        rayQueryCapabilities.maxGeometryCount = asProps.maxGeometryCount;
        rayQueryCapabilities.maxInstanceCount = asProps.maxInstanceCount;
        rayQueryCapabilities.maxPrimitiveCount = asProps.maxPrimitiveCount;

        if (debug)
        {
            vfLogDebug("Ray Query supported: {}, Acceleration Structure supported: {}",
                      rayQueryCapabilities.rayQuerySupported,
                      rayQueryCapabilities.accelerationStructureSupported);
            vfLogDebug("AS limits: maxGeometry={}, maxInstance={}, maxPrimitive={}",
                      rayQueryCapabilities.maxGeometryCount,
                      rayQueryCapabilities.maxInstanceCount,
                      rayQueryCapabilities.maxPrimitiveCount);
        }
    }

    DeviceMemoryInfo Device::getDeviceMemoryInfo() const
    {
        DeviceMemoryInfo info{};

        vk::PhysicalDeviceMemoryProperties memProps = physicalDevice.getMemoryProperties();

        // Find the largest device-local and host-visible heaps
        for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
        {
            const auto& heap = memProps.memoryHeaps[i];

            if (heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal)
            {
                if (heap.size > info.deviceLocalHeapSize)
                {
                    info.deviceLocalHeapSize = heap.size;
                }
            }
        }

        // Check memory types to find host-visible memory and detect unified memory
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            const auto& memType = memProps.memoryTypes[i];
            const auto& heap = memProps.memoryHeaps[memType.heapIndex];

            // Host-visible memory
            if (memType.propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)
            {
                if (heap.size > info.hostVisibleHeapSize)
                {
                    info.hostVisibleHeapSize = heap.size;
                }

                // Unified memory: device-local AND host-visible in same type
                if (memType.propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)
                {
                    info.hasUnifiedMemory = true;
                }
            }
        }

        return info;
    }
}
