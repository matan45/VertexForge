#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include "RenderManager.hpp" // for MAX_FRAMES_IN_FLIGHT
#include <array>

namespace core
{
    class Device;

    // Manages async compute queue submission with timeline semaphore synchronization.
    //
    // Design: async compute runs with NO wait semaphores — it reads previous-frame GPU
    // data (light buffers, cluster grid, terrain tiles, TLAS) which is acceptable for
    // all async passes (light culling, grass, GI probes, atmosphere, clouds, VFX, ocean).
    // The graphics queue waits on the compute timeline before fragment shading.
    class AsyncComputeManager
    {
    private:
        Device& device;

        struct FrameResources
        {
            vk::UniqueCommandPool commandPool;
            vk::UniqueCommandBuffer commandBuffer;
            vk::Fence fence{nullptr}; // CPU fence to ensure buffer is not in-flight before reset
        };
        std::array<FrameResources, MAX_FRAMES_IN_FLIGHT> frames;

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

        // Begin recording async compute commands for this frame.
        // Waits on the per-frame fence to ensure the previous use of this slot is complete.
        vk::CommandBuffer beginFrame(uint32_t frameIndex);

        // End recording and submit async compute work.
        // No GPU wait semaphores — reads previous-frame data by design.
        void submitComputeWork(uint32_t frameIndex);

        // Get sync info for graphics submit (waits on async compute completion)
        vk::Semaphore getComputeTimelineSemaphore() const { return computeTimeline; }
        uint64_t getComputeWaitValue() const { return computeTimelineValue; }

        // Wait for all async compute from CPU (used during resize/cleanup)
        void waitIdle();
    };
}
