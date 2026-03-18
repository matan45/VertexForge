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

        // Create per-frame command pools, buffers, and fences
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
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
                cleanUp();
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
                cleanUp();
                return;
            }

            // Per-frame fence (signaled initially so first beginFrame doesn't block)
            try
            {
                vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
                frames[i].fence = device.getLogicalDevice().createFence(fenceInfo);
            }
            catch (const vk::SystemError& err)
            {
                vfLogError("Failed to create async compute fence: {}", err.what());
                cleanUp();
                return;
            }
        }

        // Create timeline semaphore for compute->graphics synchronization
        vk::SemaphoreTypeCreateInfo timelineCreateInfo{};
        timelineCreateInfo.semaphoreType = vk::SemaphoreType::eTimeline;
        timelineCreateInfo.initialValue = 0;

        vk::SemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.pNext = &timelineCreateInfo;

        try
        {
            computeTimeline = device.getLogicalDevice().createSemaphore(semaphoreInfo);
        }
        catch (const vk::SystemError& err)
        {
            vfLogError("Failed to create timeline semaphore: {}", err.what());
            cleanUp();
            return;
        }

        enabled = true;
        vfLogInfo("Async compute manager initialized (queue family={})", computeQueueFamily);
    }

    void AsyncComputeManager::cleanUp()
    {
        // Always clean up any resources that were created, even if init failed partway
        waitIdle();

        for (auto& frame : frames)
        {
            frame.commandBuffer.reset();
            frame.commandPool.reset();
            if (frame.fence)
            {
                device.getLogicalDevice().destroyFence(frame.fence);
                frame.fence = nullptr;
            }
        }

        if (computeTimeline)
        {
            device.getLogicalDevice().destroySemaphore(computeTimeline);
            computeTimeline = nullptr;
        }

        enabled = false;
    }

    vk::CommandBuffer AsyncComputeManager::beginFrame(uint32_t frameIndex)
    {
        if (!enabled) return nullptr;

        uint32_t fi = frameIndex % MAX_FRAMES_IN_FLIGHT;
        auto& frame = frames[fi];

        // Wait for the GPU to finish with this frame slot's command buffer
        // before resetting it (prevents corrupting a buffer still in flight)
        static_cast<void>(device.getLogicalDevice().waitForFences(1, &frame.fence, VK_TRUE, UINT64_MAX));
        static_cast<void>(device.getLogicalDevice().resetFences(1, &frame.fence));

        frame.commandBuffer->reset(vk::CommandBufferResetFlagBits::eReleaseResources);

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        frame.commandBuffer->begin(beginInfo);

        return frame.commandBuffer.get();
    }

    void AsyncComputeManager::submitComputeWork(uint32_t frameIndex)
    {
        if (!enabled) return;

        uint32_t fi = frameIndex % MAX_FRAMES_IN_FLIGHT;
        auto& frame = frames[fi];
        frame.commandBuffer->end();

        computeTimelineValue++;

        // No wait semaphores — async compute reads previous-frame GPU buffers by design.
        // Light culling reads last frame's light positions (imperceptible 1-frame latency).
        // Atmosphere/clouds read CPU-set uniforms (already visible).
        // Grass reads terrain tile data (stable between frames).
        // GI probes read TLAS (rebuilt on graphics queue, stable from previous frame).
        std::array<vk::Semaphore, 1> signalSemaphores = {computeTimeline};
        std::array<uint64_t, 1> signalValues = {computeTimelineValue};

        vk::TimelineSemaphoreSubmitInfo timelineInfo{};
        timelineInfo.waitSemaphoreValueCount = 0;
        timelineInfo.pWaitSemaphoreValues = nullptr;
        timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
        timelineInfo.pSignalSemaphoreValues = signalValues.data();

        vk::SubmitInfo submitInfo{};
        submitInfo.pNext = &timelineInfo;
        submitInfo.waitSemaphoreCount = 0;
        submitInfo.pWaitSemaphores = nullptr;
        submitInfo.pWaitDstStageMask = nullptr;
        submitInfo.commandBufferCount = 1;
        vk::CommandBuffer cmd = frame.commandBuffer.get();
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
        submitInfo.pSignalSemaphores = signalSemaphores.data();

        // Submit with per-frame fence — beginFrame waits on this before resetting the buffer
        device.getAsyncComputeQueue().submit(submitInfo, frame.fence);
    }

    void AsyncComputeManager::waitIdle()
    {
        if (computeTimelineValue > 0 && computeTimeline)
        {
            vk::SemaphoreWaitInfo waitInfo{};
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &computeTimeline;
            waitInfo.pValues = &computeTimelineValue;

            static_cast<void>(device.getLogicalDevice().waitSemaphores(waitInfo, UINT64_MAX));
        }
    }
}
