#include "IndirectBatchManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/Utilities.hpp"
#include "print/Logger.hpp"
#include <cstring>

namespace render::gpudriven {

    IndirectBatchManager::IndirectBatchManager(core::Device& device)
        : device(device)
    {
    }

    IndirectBatchManager::~IndirectBatchManager()
    {
        cleanup();
    }

    void IndirectBatchManager::init(uint32_t batchCount, uint32_t commandsPerBatch)
    {
        if (initialized) {
            loggerWarning("IndirectBatchManager already initialized");
            return;
        }

        // Validate batch count
        if (batchCount == 0 || batchCount > MAX_BATCH_COUNT) {
            loggerError("Invalid batch count: {}. Must be 1-{}", batchCount, MAX_BATCH_COUNT);
            batchCount = DEFAULT_BATCH_COUNT;
        }

        this->batchCount = batchCount;
        this->commandsPerBatch = commandsPerBatch;
        createBuffers();
        initialized = true;

        loggerInfo("IndirectBatchManager initialized: {} batches x {} commands = {} total capacity",
                   this->batchCount, this->commandsPerBatch, getTotalCapacity());
        loggerInfo("  Draw command buffer: {} MB", getCombinedDrawCommandBufferSize() / (1024.0f * 1024.0f));
        loggerInfo("  Draw count buffer: {} bytes", getCombinedDrawCountBufferSize());
        loggerInfo("  Per-draw data buffer: {} MB", getCombinedPerDrawDataBufferSize() / (1024.0f * 1024.0f));
    }

    void IndirectBatchManager::cleanup()
    {
        if (!initialized) return;

        device.getLogicalDevice().waitIdle();
        destroyBuffers();
        initialized = false;

        loggerInfo("IndirectBatchManager cleaned up");
    }

    void IndirectBatchManager::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        // Combined draw command buffer - all batches contiguous
        // Written by compute shader, read by vkCmdDrawIndexedIndirectCount
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = getCombinedDrawCommandBufferSize();
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |      // Compute shader writes
                           vk::BufferUsageFlagBits::eIndirectBuffer |       // Indirect draw reads
                           vk::BufferUsageFlagBits::eTransferDst;           // Clear/reset
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, combinedDrawCommandBuffer, combinedDrawCommandMemory);
        }

        // Combined draw count buffer - BatchDrawStats for each batch
        // Layout: [Batch0 stats][Batch1 stats]...[BatchN stats]
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = getCombinedDrawCountBufferSize();
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |       // Compute shader atomic
                           vk::BufferUsageFlagBits::eIndirectBuffer |       // Count for indirect
                           vk::BufferUsageFlagBits::eTransferDst |          // Reset to 0
                           vk::BufferUsageFlagBits::eTransferSrc;           // Readback for debug
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, combinedDrawCountBuffer, combinedDrawCountMemory);
        }

        // Combined per-draw data buffer - all batches contiguous
        // Written by compute shader, read by vertex/fragment shaders
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = getCombinedPerDrawDataBufferSize();
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |       // Compute writes, VS/FS reads
                           vk::BufferUsageFlagBits::eTransferDst;           // Clear if needed
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, combinedPerDrawDataBuffer, combinedPerDrawDataMemory);
        }

        // Staging buffer for count reset and readback (needs to hold all batch stats)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = getCombinedDrawCountBufferSize();
            request.usage = vk::BufferUsageFlagBits::eTransferSrc |
                           vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::Utilities::createBuffer(request, stagingBuffer, stagingMemory);

            stagingMapped = logicalDevice.mapMemory(
                stagingMemory, 0, getCombinedDrawCountBufferSize(), vk::MemoryMapFlags{}
            );
            // Initialize all stats to 0
            std::memset(stagingMapped, 0, getCombinedDrawCountBufferSize());
        }
    }

    void IndirectBatchManager::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (stagingMapped) {
            logicalDevice.unmapMemory(stagingMemory);
            stagingMapped = nullptr;
        }

        if (stagingBuffer) {
            logicalDevice.destroyBuffer(stagingBuffer);
            logicalDevice.freeMemory(stagingMemory);
            stagingBuffer = nullptr;
        }

        if (combinedPerDrawDataBuffer) {
            logicalDevice.destroyBuffer(combinedPerDrawDataBuffer);
            logicalDevice.freeMemory(combinedPerDrawDataMemory);
            combinedPerDrawDataBuffer = nullptr;
        }

        if (combinedDrawCountBuffer) {
            logicalDevice.destroyBuffer(combinedDrawCountBuffer);
            logicalDevice.freeMemory(combinedDrawCountMemory);
            combinedDrawCountBuffer = nullptr;
        }

        if (combinedDrawCommandBuffer) {
            logicalDevice.destroyBuffer(combinedDrawCommandBuffer);
            logicalDevice.freeMemory(combinedDrawCommandMemory);
            combinedDrawCommandBuffer = nullptr;
        }
    }

    void IndirectBatchManager::resetAllBatches(vk::CommandBuffer cmd)
    {
        // Reset all batch stats to 0
        std::memset(stagingMapped, 0, getCombinedDrawCountBufferSize());

        // Copy zeros from staging to draw count buffer
        vk::BufferCopy copyRegion;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = getCombinedDrawCountBufferSize();
        cmd.copyBuffer(stagingBuffer, combinedDrawCountBuffer, copyRegion);

        // Barrier to ensure copy completes before compute shader
        vk::BufferMemoryBarrier barrier;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = combinedDrawCountBuffer;
        barrier.offset = 0;
        barrier.size = getCombinedDrawCountBufferSize();

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {},
            {},
            barrier,
            {}
        );
    }

    void IndirectBatchManager::insertBarriersAfterCompute(vk::CommandBuffer cmd)
    {
        // Barrier between compute shader writes and indirect draw reads
        std::array<vk::BufferMemoryBarrier, 3> barriers;

        // Draw command buffer: compute write -> indirect read
        barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].buffer = combinedDrawCommandBuffer;
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;

        // Draw count buffer: compute write -> indirect read
        // Note: We need to read drawCount from each batch, so use whole buffer
        barriers[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].buffer = combinedDrawCountBuffer;
        barriers[1].offset = 0;
        barriers[1].size = VK_WHOLE_SIZE;

        // Per-draw data buffer: compute write -> vertex/fragment shader read
        barriers[2].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[2].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[2].buffer = combinedPerDrawDataBuffer;
        barriers[2].offset = 0;
        barriers[2].size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexShader,
            {},
            {},
            barriers,
            {}
        );
    }

    std::vector<BatchDrawStats> IndirectBatchManager::readBackAllStats()
    {
        // This is expensive - causes a full GPU sync
        // Only use for debugging

        device.getLogicalDevice().waitIdle();

        // Copy from GPU to staging
        auto cmdBuffer = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(),
            device.getStagingCommandPool()
        );

        vk::BufferCopy copyRegion;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = getCombinedDrawCountBufferSize();
        cmdBuffer->copyBuffer(combinedDrawCountBuffer, stagingBuffer, copyRegion);

        core::Utilities::endSingleTimeCommands(
            device.getGraphicsQueue(),
            cmdBuffer
        );

        // Read from staging into vector
        std::vector<BatchDrawStats> stats(batchCount);
        std::memcpy(stats.data(), stagingMapped, getCombinedDrawCountBufferSize());

        return stats;
    }

    GPUDrivenStats IndirectBatchManager::readBackAggregatedStats()
    {
        auto batchStats = readBackAllStats();

        GPUDrivenStats aggregated{};
        aggregated.drawCalls = batchCount;  // One draw call per batch

        for (const auto& batch : batchStats) {
            aggregated.visibleObjects += batch.drawCount;
            aggregated.objectsLOD0 += batch.lodCount0;
            aggregated.objectsLOD1 += batch.lodCount1;
            aggregated.objectsLOD2 += batch.lodCount2;
            aggregated.objectsLOD3 += batch.lodCount3;
            aggregated.culledByFrustum += batch.culledByFrustum;
            aggregated.culledByOcclusion += batch.culledByOcclusion;
        }

        return aggregated;
    }

}
