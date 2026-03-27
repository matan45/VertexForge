#pragma once

#include <vector>
#include <array>
#include <mutex>
#include <filesystem>

#include "Utilities.hpp"

namespace window
{
    class Window;
}

namespace core
{
    // GPU memory information for resource allocation decisions
    struct DeviceMemoryInfo
    {
        vk::DeviceSize deviceLocalHeapSize = 0; // Total device-local VRAM
        vk::DeviceSize hostVisibleHeapSize = 0; // Host-visible memory
        bool hasUnifiedMemory = false; // APU/integrated GPU
    };

    struct MeshShaderCapabilities
    {
        // Feature support flags
        bool meshShaderSupported = false;
        bool taskShaderSupported = false;
        bool meshShaderQueriesSupported = false;

        uint32_t maxMeshOutputVertices = 0;
        uint32_t maxMeshOutputPrimitives = 0;
        uint32_t maxMeshWorkGroupInvocations = 0;
        std::array<uint32_t, 3> maxMeshWorkGroupSize = {0, 0, 0};
        uint32_t maxMeshOutputMemorySize = 0;
        uint32_t maxMeshPayloadAndOutputMemorySize = 0;

        uint32_t maxTaskWorkGroupInvocations = 0;
        std::array<uint32_t, 3> maxTaskWorkGroupSize = {0, 0, 0};
        uint32_t maxTaskPayloadSize = 0;

        uint32_t maxPreferredMeshWorkGroupInvocations = 0;
        uint32_t maxPreferredTaskWorkGroupInvocations = 0;
    };

    struct RayQueryCapabilities
    {
        bool rayQuerySupported = false;
        bool accelerationStructureSupported = false;
        uint64_t maxGeometryCount = 0;
        uint64_t maxInstanceCount = 0;
        uint64_t maxPrimitiveCount = 0;
    };

    class Device
    {
    private:
        const window::Window* window;

        vk::UniqueInstance instance{nullptr};
        vk::PhysicalDevice physicalDevice{nullptr};
        vk::UniqueDevice logicalDevice{nullptr};

        vk::DebugUtilsMessengerEXT debugMessenger{nullptr};
        vk::detail::DispatchLoaderDynamic dldi;

        vk::SurfaceKHR surface{nullptr};
        vk::Queue presentQueue{nullptr};
        vk::Queue graphicsAndComputeQueue{nullptr};
        vk::Queue transferQueue{nullptr};
        vk::Queue asyncComputeQueue{nullptr};

        QueueFamilyIndices queueFamilyIndices{};

        MeshShaderCapabilities meshShaderCapabilities{};
        RayQueryCapabilities rayQueryCapabilities{};

        // Shared staging command pool for one-time transfer operations
        vk::UniqueCommandPool stagingCommandPool;

        // Pipeline cache for faster pipeline creation on subsequent launches
        vk::UniquePipelineCache pipelineCache;
        std::filesystem::path pipelineCachePath;

        mutable std::mutex graphicsQueueMutex;
        mutable std::mutex transferQueueMutex;

        const std::array<const char*, 1> validationLayers = {"VK_LAYER_KHRONOS_validation"};
        const std::array<const char*, 5> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_EXT_MESH_SHADER_EXTENSION_NAME,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_QUERY_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
        };

        // Private functions for setup and initialization
        void createInstance();
        std::vector<const char*> getRequiredExtensions() const;
        void createDebugMessenger();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createStagingCommandPool();
        void createPipelineCache();
        void loadPipelineCacheFromDisk();
        void queryMeshShaderCapabilities();
        void queryRayQueryCapabilities();
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

        const vk::Queue& getAsyncComputeQueue() const { return asyncComputeQueue; }
        bool hasAsyncComputeQueue() const { return queueFamilyIndices.hasAsyncComputeQueue(); }

        // Lock before submitting to the graphics queue from any thread
        std::mutex& getGraphicsQueueMutex() const { return graphicsQueueMutex; }

        // Thread-safe graphics queue submit (locks internally)
        void submitGraphics(const vk::SubmitInfo& submitInfo, vk::Fence fence = nullptr) const
        {
            std::lock_guard lock(graphicsQueueMutex);
            graphicsAndComputeQueue.submit(submitInfo, fence);
        }

        void waitGraphicsIdle() const
        {
            std::lock_guard lock(graphicsQueueMutex);
            graphicsAndComputeQueue.waitIdle();
        }

        void submitTransfer(const vk::SubmitInfo& submitInfo, vk::Fence fence = nullptr) const
        {
            auto& mtx = queueFamilyIndices.hasDedicatedTransferQueue() ? transferQueueMutex : graphicsQueueMutex;
            std::lock_guard lock(mtx);
            transferQueue.submit(submitInfo, fence);
        }

        void waitTransferIdle() const
        {
            auto& mtx = queueFamilyIndices.hasDedicatedTransferQueue() ? transferQueueMutex : graphicsQueueMutex;
            std::lock_guard lock(mtx);
            transferQueue.waitIdle();
        }

        const vk::CommandPool& getStagingCommandPool() const { return stagingCommandPool.get(); }

        vk::PipelineCache getPipelineCache() const { return pipelineCache.get(); }
        void savePipelineCacheToDisk() const;

        DeviceMemoryInfo getDeviceMemoryInfo() const;

        bool isMeshShaderSupported() const { return meshShaderCapabilities.meshShaderSupported; }
        bool isTaskShaderSupported() const { return meshShaderCapabilities.taskShaderSupported; }
        const MeshShaderCapabilities& getMeshShaderCapabilities() const { return meshShaderCapabilities; }

        bool isRayQuerySupported() const { return rayQueryCapabilities.rayQuerySupported; }
        bool isAccelerationStructureSupported() const { return rayQueryCapabilities.accelerationStructureSupported; }
        const RayQueryCapabilities& getRayQueryCapabilities() const { return rayQueryCapabilities; }
    };
}
