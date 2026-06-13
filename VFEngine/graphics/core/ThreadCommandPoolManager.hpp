#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include "GraphicsConstants.hpp"
#include <vector>
#include <array>

namespace core
{
    class Device;

    class ThreadCommandPoolManager
    {
    public:
        // Independent secondary-buffer slots per (thread, frame). A primary that executes
        // secondaries is invalidated if those secondaries are re-recorded before submit, so
        // each distinct execute-into-the-same-primary pass needs its own slot. The shadow
        // pass uses two (static layer + dynamic layer) within one frame.
        static constexpr uint32_t SECONDARY_SLOTS_PER_FRAME = 2;

    private:
        Device* device = nullptr;

        struct ThreadPool
        {
            vk::UniqueCommandPool commandPool;
            // Indexed [swapchain image index][slot]. Sized by image count (not frames in
            // flight) because the shadow pass records/resets these by acquired image index,
            // guarded by the engine's per-image fence (RenderManager::imagesInFlight).
            std::array<std::array<vk::UniqueCommandBuffer, SECONDARY_SLOTS_PER_FRAME>,
                       MAX_SWAPCHAIN_IMAGES> secondaryBuffers;
        };

        std::vector<ThreadPool> threadPools;
        uint32_t threadCount = 0;

    public:
        ThreadCommandPoolManager() = default;
        ~ThreadCommandPoolManager() = default;

        ThreadCommandPoolManager(const ThreadCommandPoolManager&) = delete;
        ThreadCommandPoolManager& operator=(const ThreadCommandPoolManager&) = delete;

        void init(Device& device, uint32_t threadCount);
        void cleanUp();

        // Reset all command pools for the given frame (call at frame start)
        void resetFrame(uint32_t frameIndex);

        // Get a secondary command buffer for the given thread, frame, and slot.
        // Different slots return distinct buffers so multiple secondary-execution passes
        // can coexist on the same primary within a frame without invalidating it.
        vk::CommandBuffer getSecondary(uint32_t threadNum, uint32_t frameIndex, uint32_t slot = 0);

        uint32_t getThreadCount() const { return threadCount; }
    };
}
