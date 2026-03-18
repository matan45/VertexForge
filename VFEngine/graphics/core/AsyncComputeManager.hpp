#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <array>

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

        // Timeline semaphore: async compute signals when work is done
        // Graphics queue waits on this before fragment shading
        vk::Semaphore computeTimeline{nullptr};
        uint64_t computeTimelineValue = 0;

        bool enabled = false;

    public:
        explicit AsyncComputeManager(Device& device);
        ~AsyncComputeManager() = default;

        void init();
        void cleanUp();

        bool isEnabled() const { return enabled; }

        // Begin recording async compute commands for this frame
        vk::CommandBuffer beginFrame(uint32_t frameIndex);

        // End recording and submit async compute work (no wait — runs immediately)
        void submitComputeWork(uint32_t frameIndex);

        // Get sync info for graphics submit (waits on async compute completion)
        vk::Semaphore getComputeTimelineSemaphore() const { return computeTimeline; }
        uint64_t getComputeWaitValue() const { return computeTimelineValue; }

        // Wait for async compute from CPU (used during resize/cleanup)
        void waitIdle();
    };
}
