#include "ThreadCommandPoolManager.hpp"
#include "Device.hpp"
#include "print/Log.hpp"

namespace core
{
    void ThreadCommandPoolManager::init(Device& dev, uint32_t numThreads)
    {
        device = &dev;
        threadCount = numThreads;

        if (threadCount == 0)
        {
            vfLogWarning("ThreadCommandPoolManager: threadCount is 0, no pools created");
            return;
        }

        uint32_t queueFamily = device->getQueueFamilyIndices().graphicsAndComputeFamily.value();

        threadPools.resize(threadCount);

        for (uint32_t t = 0; t < threadCount; t++)
        {
            vk::CommandPoolCreateInfo poolInfo{};
            poolInfo.queueFamilyIndex = queueFamily;
            poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                             vk::CommandPoolCreateFlagBits::eTransient;

            try
            {
                threadPools[t].commandPool = device->getLogicalDevice().createCommandPoolUnique(poolInfo);
            }
            catch (const vk::SystemError& err)
            {
                vfLogError("Failed to create thread command pool {}: {}", t, err.what());
                return;
            }

            // Allocate secondary command buffers (one per frame-in-flight per slot)
            vk::CommandBufferAllocateInfo allocInfo{};
            allocInfo.commandPool = threadPools[t].commandPool.get();
            allocInfo.level = vk::CommandBufferLevel::eSecondary;
            allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT * SECONDARY_SLOTS_PER_FRAME;

            try
            {
                auto buffers = device->getLogicalDevice().allocateCommandBuffersUnique(allocInfo);
                for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; f++)
                    for (uint32_t s = 0; s < SECONDARY_SLOTS_PER_FRAME; s++)
                        threadPools[t].secondaryBuffers[f][s] =
                            std::move(buffers[f * SECONDARY_SLOTS_PER_FRAME + s]);
            }
            catch (const vk::SystemError& err)
            {
                vfLogError("Failed to allocate secondary command buffers for thread {}: {}", t, err.what());
                return;
            }
        }

        vfLogInfo("ThreadCommandPoolManager initialized: {} threads, {} frames", threadCount, MAX_FRAMES_IN_FLIGHT);
    }

    void ThreadCommandPoolManager::cleanUp()
    {
        for (auto& pool : threadPools)
        {
            for (auto& frameSlots : pool.secondaryBuffers)
                for (auto& buf : frameSlots)
                    buf.reset();
            pool.commandPool.reset();
        }
        threadPools.clear();
        threadCount = 0;
    }

    void ThreadCommandPoolManager::resetFrame(uint32_t frameIndex)
    {
        uint32_t fi = frameIndex % MAX_FRAMES_IN_FLIGHT;

        for (uint32_t t = 0; t < threadCount; t++)
        {
            // Reset every slot's secondary buffer for this frame
            for (uint32_t s = 0; s < SECONDARY_SLOTS_PER_FRAME; s++)
                threadPools[t].secondaryBuffers[fi][s]->reset(vk::CommandBufferResetFlagBits::eReleaseResources);
        }
    }

    vk::CommandBuffer ThreadCommandPoolManager::getSecondary(uint32_t threadNum, uint32_t frameIndex, uint32_t slot)
    {
        uint32_t fi = frameIndex % MAX_FRAMES_IN_FLIGHT;
        uint32_t sl = slot % SECONDARY_SLOTS_PER_FRAME;
        return threadPools[threadNum].secondaryBuffers[fi][sl].get();
    }
}
