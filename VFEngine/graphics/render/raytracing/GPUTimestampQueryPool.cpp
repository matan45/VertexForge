#include "GPUTimestampQueryPool.hpp"
#include "../../core/Device.hpp"
#include "../../../utilities/print/Log.hpp"

namespace render::raytracing
{
    bool GPUTimestampQueryPool::init(core::Device& device, uint32_t numQueries)
    {
        const auto& physDevice = device.getPhysicalDevice();
        const auto& logDevice = device.getLogicalDevice();

        // Check if timestamps are supported on the graphics queue family
        auto queueProps = physDevice.getQueueFamilyProperties();
        uint32_t graphicsFamily = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        uint32_t timestampValidBits = queueProps[graphicsFamily].timestampValidBits;

        if (timestampValidBits == 0)
        {
            vfLogWarning("GPU does not support timestamp queries on graphics queue");
            valid = false;
            return false;
        }

        auto props = physDevice.getProperties();
        timestampPeriod = props.limits.timestampPeriod; // nanoseconds per tick
        queryCount = numQueries;

        vk::QueryPoolCreateInfo poolInfo{};
        poolInfo.queryType = vk::QueryType::eTimestamp;
        poolInfo.queryCount = queryCount;

        for (auto& pool : pools)
        {
            pool = logDevice.createQueryPool(poolInfo);
        }

        valid = true;
        vfLogInfo("GPU timestamp query pool created: {} queries, period={:.2f}ns",
                  queryCount, timestampPeriod);
        return true;
    }

    void GPUTimestampQueryPool::cleanup(const vk::Device& logicalDevice)
    {
        for (auto& pool : pools)
        {
            if (pool)
            {
                logicalDevice.destroyQueryPool(pool);
                pool = nullptr;
            }
        }
        valid = false;
    }

    void GPUTimestampQueryPool::resetFrame(vk::CommandBuffer cmd, uint32_t frameIndex)
    {
        if (!valid) return;
        uint32_t fi = frameIndex % core::MAX_FRAMES_IN_FLIGHT;
        cmd.resetQueryPool(pools[fi], 0, queryCount);
    }

    void GPUTimestampQueryPool::writeTimestamp(vk::CommandBuffer cmd, uint32_t frameIndex,
                                               uint32_t queryIndex, vk::PipelineStageFlagBits stage)
    {
        if (!valid || queryIndex >= queryCount) return;
        uint32_t fi = frameIndex % core::MAX_FRAMES_IN_FLIGHT;
        cmd.writeTimestamp(stage, pools[fi], queryIndex);
    }

    bool GPUTimestampQueryPool::readResults(const vk::Device& logicalDevice, uint32_t frameIndex,
                                            std::vector<uint64_t>& outTimestamps) const
    {
        if (!valid) return false;
        uint32_t fi = frameIndex % core::MAX_FRAMES_IN_FLIGHT;

        outTimestamps.resize(queryCount);

        // Non-blocking read — some queries may not have been written this frame
        // (e.g., BLAS/TLAS build timestamps are conditional). Using eWait would
        // deadlock on unwritten queries after resize or frames without builds.
        auto result = logicalDevice.getQueryPoolResults(
            pools[fi],
            0, queryCount,
            queryCount * sizeof(uint64_t),
            outTimestamps.data(),
            sizeof(uint64_t),
            vk::QueryResultFlagBits::e64);

        return result == vk::Result::eSuccess;
    }

    float GPUTimestampQueryPool::toMilliseconds(uint64_t startTimestamp, uint64_t endTimestamp) const
    {
        if (!valid || endTimestamp < startTimestamp) return 0.0f;
        double deltaNs = static_cast<double>(endTimestamp - startTimestamp) * static_cast<double>(timestampPeriod);
        return static_cast<float>(deltaNs / 1'000'000.0);
    }
}
