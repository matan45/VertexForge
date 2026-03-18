#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <array>
#include <functional>

namespace core
{
    class Device;

    constexpr uint32_t ASYNC_COMPUTE_FRAMES = 2; // Must match MAX_FRAMES_IN_FLIGHT

    class AsyncComputeManager
    {
    private:
        Device& device;

        struct FrameResources
        {
            vk::UniqueCommandPool commandPool;
            vk::UniqueCommandBuffer commandBuffer;
        };
        std::array<FrameResources, ASYNC_COMPUTE_FRAMES> frames;

        // Timeline semaphores for cross-queue synchronization
        vk::Semaphore computeTimeline{nullptr};  // Async compute signals when work is done
        vk::Semaphore graphicsTimeline{nullptr};  // Graphics signals when uploads are done

        uint64_t computeTimelineValue = 0;
        uint64_t graphicsTimelineValue = 0;

        bool enabled = false;

    public:
        explicit AsyncComputeManager(Device& device);
        ~AsyncComputeManager() = default;

        void init();
        void cleanUp();

        bool isEnabled() const { return enabled; }

        // Begin recording async compute commands for this frame
        vk::CommandBuffer beginFrame(uint32_t frameIndex);

        // End recording and submit async compute work
        // Waits on graphicsTimeline at waitGraphicsValue before executing
        void submitComputeWork(uint32_t frameIndex);

        // Called by graphics queue after uploads are done
        // Returns the timeline value that was signaled
        uint64_t signalGraphicsReady();

        // Get sync info for graphics submit (waits on async compute completion)
        vk::Semaphore getComputeTimelineSemaphore() const { return computeTimeline; }
        uint64_t getComputeWaitValue() const { return computeTimelineValue; }

        vk::Semaphore getGraphicsTimelineSemaphore() const { return graphicsTimeline; }
        uint64_t getGraphicsWaitValue() const { return graphicsTimelineValue; }

        // Wait for async compute from CPU (used during resize/cleanup)
        void waitIdle();
    };
}
