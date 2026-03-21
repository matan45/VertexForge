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
    private:
        Device* device = nullptr;

        struct ThreadPool
        {
            vk::UniqueCommandPool commandPool;
            std::array<vk::UniqueCommandBuffer, MAX_FRAMES_IN_FLIGHT> secondaryBuffers;
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

        // Get a secondary command buffer for the given thread and frame
        vk::CommandBuffer getSecondary(uint32_t threadNum, uint32_t frameIndex);

        uint32_t getThreadCount() const { return threadCount; }
    };
}
