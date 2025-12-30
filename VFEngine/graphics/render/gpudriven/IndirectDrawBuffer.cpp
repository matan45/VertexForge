#include "IndirectDrawBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "print/Logger.hpp"
#include <cstring>

namespace render::gpudriven
{
    IndirectDrawBuffer::IndirectDrawBuffer(core::Device& device)
        : device(device)
    {
    }

    IndirectDrawBuffer::~IndirectDrawBuffer()
    {
        cleanup();
    }

    void IndirectDrawBuffer::init(uint32_t maxCommands)
    {
        if (initialized)
        {
            loggerWarning("IndirectDrawBuffer already initialized");
            return;
        }

        maxDrawCommands = maxCommands;
        createBuffers();
        initialized = true;

        loggerInfo("IndirectDrawBuffer initialized: {} max draw commands", maxDrawCommands);
    }

    void IndirectDrawBuffer::cleanup()
    {
        if (!initialized) return;

        device.getLogicalDevice().waitIdle();
        destroyBuffers();
        initialized = false;

        loggerInfo("IndirectDrawBuffer cleaned up");
    }

    void IndirectDrawBuffer::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();
        
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxDrawCommands * sizeof(DrawIndexedIndirectCommand);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | // Compute shader writes
                vk::BufferUsageFlagBits::eIndirectBuffer | // Indirect draw reads
                vk::BufferUsageFlagBits::eTransferDst; // Clear/reset
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, drawCommandBuffer, drawCommandMemory);
        }
        
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = sizeof(GPUCullStats);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | // Compute shader atomic
                vk::BufferUsageFlagBits::eIndirectBuffer | // Count for indirect
                vk::BufferUsageFlagBits::eTransferDst | // Reset to 0
                vk::BufferUsageFlagBits::eTransferSrc; // Readback for debug
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, drawCountBuffer, drawCountMemory);
        }
        
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxDrawCommands * sizeof(PerDrawData);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | // Compute writes, VS/FS reads
                vk::BufferUsageFlagBits::eTransferDst; // Clear if needed
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, perDrawDataBuffer, perDrawDataMemory);
        }
        
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = sizeof(GPUCullStats);
            request.usage = vk::BufferUsageFlagBits::eTransferSrc |
                vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, stagingBuffer, stagingMemory);

            stagingMapped = logicalDevice.mapMemory(
                stagingMemory, 0, sizeof(GPUCullStats), vk::MemoryMapFlags{}
            );
            
            std::memset(stagingMapped, 0, sizeof(GPUCullStats));
        }
    }

    void IndirectDrawBuffer::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (stagingMapped)
        {
            logicalDevice.unmapMemory(stagingMemory);
            stagingMapped = nullptr;
        }

        core::BufferUtilities::destroyBuffer(logicalDevice, stagingBuffer, stagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, perDrawDataBuffer, perDrawDataMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, drawCountBuffer, drawCountMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, drawCommandBuffer, drawCommandMemory);
    }

    void IndirectDrawBuffer::resetDrawCount(vk::CommandBuffer cmd)
    {
        // Reset all stats to 0 (drawCount + LOD counts)
        std::memset(stagingMapped, 0, sizeof(GPUCullStats));

        // Copy zeros from staging to draw count buffer
        vk::BufferCopy copyRegion;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = sizeof(GPUCullStats);
        cmd.copyBuffer(stagingBuffer, drawCountBuffer, copyRegion);

        // Barrier to ensure copy completes before compute shader
        vk::BufferMemoryBarrier barrier;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = drawCountBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(GPUCullStats);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {},
            {},
            barrier,
            {}
        );
    }

    void IndirectDrawBuffer::insertBarrierAfterCompute(vk::CommandBuffer cmd)
    {
        std::array<vk::BufferMemoryBarrier, 3> barriers;
        
        barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].buffer = drawCommandBuffer;
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;
        
        barriers[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].buffer = drawCountBuffer;
        barriers[1].offset = 0;
        barriers[1].size = sizeof(uint32_t);
        
        barriers[2].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[2].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[2].buffer = perDrawDataBuffer;
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

    uint32_t IndirectDrawBuffer::readBackDrawCount()
    {
        GPUCullStats stats = readBackStats();
        return stats.drawCount;
    }

    GPUCullStats IndirectDrawBuffer::readBackStats()
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
        copyRegion.size = sizeof(GPUCullStats);
        cmdBuffer->copyBuffer(drawCountBuffer, stagingBuffer, copyRegion);

        core::Utilities::endSingleTimeCommands(
            device.getGraphicsQueue(),
            cmdBuffer
        );

        // Read from staging
        return *static_cast<GPUCullStats*>(stagingMapped);
    }
}
