#include "AsyncComputeManager.hpp"
#include "Device.hpp"
#include "print/Log.hpp"

namespace core
{
    AsyncComputeManager::AsyncComputeManager(Device& device)
        : device{device}
    {
    }

    void AsyncComputeManager::init()
    {
        if (!device.hasAsyncComputeQueue())
        {
            vfLogInfo("No async compute queue available - async compute disabled");
            enabled = false;
            return;
        }

        const auto& indices = device.getQueueFamilyIndices();
        uint32_t computeQueueFamily = indices.asyncComputeFamily.value();

        // Create per-frame command pools and buffers
        for (uint32_t i = 0; i < ASYNC_COMPUTE_FRAMES; i++)
        {
            vk::CommandPoolCreateInfo poolInfo{};
            poolInfo.queueFamilyIndex = computeQueueFamily;
            poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                             vk::CommandPoolCreateFlagBits::eTransient;

            try
            {
                frames[i].commandPool = device.getLogicalDevice().createCommandPoolUnique(poolInfo);
            }
            catch (const vk::SystemError& err)
            {
                vfLogError("Failed to create async compute command pool: {}", err.what());
                enabled = false;
                return;
            }

            vk::CommandBufferAllocateInfo allocInfo{};
            allocInfo.commandPool = frames[i].commandPool.get();
            allocInfo.level = vk::CommandBufferLevel::ePrimary;
            allocInfo.commandBufferCount = 1;

            try
            {
                auto buffers = device.getLogicalDevice().allocateCommandBuffersUnique(allocInfo);
                frames[i].commandBuffer = std::move(buffers[0]);
            }
            catch (const vk::SystemError& err)
            {
                vfLogError("Failed to allocate async compute command buffer: {}", err.what());
                enabled = false;
                return;
            }
        }

        // Create timeline semaphores
        vk::SemaphoreTypeCreateInfo timelineCreateInfo{};
        timelineCreateInfo.semaphoreType = vk::SemaphoreType::eTimeline;
        timelineCreateInfo.initialValue = 0;

        vk::SemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.pNext = &timelineCreateInfo;

        try
        {
            computeTimeline = device.getLogicalDevice().createSemaphore(semaphoreInfo);
            graphicsTimeline = device.getLogicalDevice().createSemaphore(semaphoreInfo);
        }
        catch (const vk::SystemError& err)
        {
            vfLogError("Failed to create timeline semaphores: {}", err.what());
            enabled = false;
            return;
        }

        enabled = true;
        vfLogInfo("Async compute manager initialized (queue family={})", computeQueueFamily);
    }

    void AsyncComputeManager::cleanUp()
    {
        if (!enabled) return;

        waitIdle();

        for (auto& frame : frames)
        {
            frame.commandBuffer.reset();
            frame.commandPool.reset();
        }

        if (computeTimeline)
        {
            device.getLogicalDevice().destroySemaphore(computeTimeline);
            computeTimeline = nullptr;
        }
        if (graphicsTimeline)
        {
            device.getLogicalDevice().destroySemaphore(graphicsTimeline);
            graphicsTimeline = nullptr;
        }

        enabled = false;
    }

    vk::CommandBuffer AsyncComputeManager::beginFrame(uint32_t frameIndex)
    {
        auto& frame = frames[frameIndex];

        frame.commandBuffer->reset(vk::CommandBufferResetFlagBits::eReleaseResources);

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        frame.commandBuffer->begin(beginInfo);

        return frame.commandBuffer.get();
    }

    void AsyncComputeManager::submitComputeWork(uint32_t frameIndex)
    {
        auto& frame = frames[frameIndex];
        frame.commandBuffer->end();

        computeTimelineValue++;

        // Build timeline semaphore submit info
        // Wait on graphicsTimeline (uploads done), signal computeTimeline (compute done)
        std::array<vk::Semaphore, 1> waitSemaphores = {graphicsTimeline};
        std::array<uint64_t, 1> waitValues = {graphicsTimelineValue};
        std::array<vk::PipelineStageFlags, 1> waitStages = {vk::PipelineStageFlagBits::eComputeShader};

        std::array<vk::Semaphore, 1> signalSemaphores = {computeTimeline};
        std::array<uint64_t, 1> signalValues = {computeTimelineValue};

        vk::TimelineSemaphoreSubmitInfo timelineInfo{};
        timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(waitValues.size());
        timelineInfo.pWaitSemaphoreValues = waitValues.data();
        timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
        timelineInfo.pSignalSemaphoreValues = signalValues.data();

        vk::SubmitInfo submitInfo{};
        submitInfo.pNext = &timelineInfo;
        submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
        submitInfo.pWaitSemaphores = waitSemaphores.data();
        submitInfo.pWaitDstStageMask = waitStages.data();
        submitInfo.commandBufferCount = 1;
        vk::CommandBuffer cmd = frame.commandBuffer.get();
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
        submitInfo.pSignalSemaphores = signalSemaphores.data();

        device.getAsyncComputeQueue().submit(submitInfo);
    }

    uint64_t AsyncComputeManager::signalGraphicsReady()
    {
        graphicsTimelineValue++;
        return graphicsTimelineValue;
    }

    void AsyncComputeManager::waitIdle()
    {
        if (!enabled) return;

        if (computeTimelineValue > 0)
        {
            vk::SemaphoreWaitInfo waitInfo{};
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &computeTimeline;
            waitInfo.pValues = &computeTimelineValue;

            device.getLogicalDevice().waitSemaphores(waitInfo, UINT64_MAX);
        }
    }
}
