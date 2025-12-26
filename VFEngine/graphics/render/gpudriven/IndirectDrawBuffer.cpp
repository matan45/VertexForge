#include "IndirectDrawBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/Utilities.hpp"
#include "print/Logger.hpp"
#include <cstring>

namespace render::gpudriven {

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
        if (initialized) {
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

        // Draw command buffer - written by compute shader, read by vkCmdDrawIndexedIndirectCount
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxDrawCommands * sizeof(DrawIndexedIndirectCommand);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |      // Compute shader writes
                           vk::BufferUsageFlagBits::eIndirectBuffer |       // Indirect draw reads
                           vk::BufferUsageFlagBits::eTransferDst;           // Clear/reset
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, drawCommandBuffer, drawCommandMemory);
        }

        // Draw count buffer - atomic counter incremented by compute, read by indirect count
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |       // Compute shader atomic
                           vk::BufferUsageFlagBits::eIndirectBuffer |       // Count for indirect
                           vk::BufferUsageFlagBits::eTransferDst |          // Reset to 0
                           vk::BufferUsageFlagBits::eTransferSrc;           // Readback for debug
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, drawCountBuffer, drawCountMemory);
        }

        // Per-draw data buffer - written by compute shader, read by vertex/fragment shaders
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxDrawCommands * sizeof(PerDrawData);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |       // Compute writes, VS/FS reads
                           vk::BufferUsageFlagBits::eTransferDst;           // Clear if needed
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, perDrawDataBuffer, perDrawDataMemory);
        }

        // Staging buffer for count reset and readback
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eTransferSrc |
                           vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::Utilities::createBuffer(request, stagingBuffer, stagingMemory);

            stagingMapped = logicalDevice.mapMemory(
                stagingMemory, 0, sizeof(uint32_t), vk::MemoryMapFlags{}
            );
            // Initialize to 0
            std::memset(stagingMapped, 0, sizeof(uint32_t));
        }
    }

    void IndirectDrawBuffer::destroyBuffers()
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

        if (perDrawDataBuffer) {
            logicalDevice.destroyBuffer(perDrawDataBuffer);
            logicalDevice.freeMemory(perDrawDataMemory);
            perDrawDataBuffer = nullptr;
        }

        if (drawCountBuffer) {
            logicalDevice.destroyBuffer(drawCountBuffer);
            logicalDevice.freeMemory(drawCountMemory);
            drawCountBuffer = nullptr;
        }

        if (drawCommandBuffer) {
            logicalDevice.destroyBuffer(drawCommandBuffer);
            logicalDevice.freeMemory(drawCommandMemory);
            drawCommandBuffer = nullptr;
        }
    }

    void IndirectDrawBuffer::resetDrawCount(vk::CommandBuffer cmd)
    {
        // Ensure staging has 0
        *static_cast<uint32_t*>(stagingMapped) = 0;

        // Copy 0 from staging to draw count buffer
        vk::BufferCopy copyRegion;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = sizeof(uint32_t);
        cmd.copyBuffer(stagingBuffer, drawCountBuffer, copyRegion);

        // Barrier to ensure copy completes before compute shader
        vk::BufferMemoryBarrier barrier;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = drawCountBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(uint32_t);

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
        // Barrier between compute shader writes and indirect draw reads
        std::array<vk::BufferMemoryBarrier, 3> barriers;

        // Draw command buffer: compute write -> indirect read
        barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].buffer = drawCommandBuffer;
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;

        // Draw count buffer: compute write -> indirect read
        barriers[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].buffer = drawCountBuffer;
        barriers[1].offset = 0;
        barriers[1].size = sizeof(uint32_t);

        // Per-draw data buffer: compute write -> vertex/fragment shader read
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
        copyRegion.size = sizeof(uint32_t);
        cmdBuffer->copyBuffer(drawCountBuffer, stagingBuffer, copyRegion);

        core::Utilities::endSingleTimeCommands(
            device.getGraphicsQueue(),
            cmdBuffer
        );

        // Read from staging
        return *static_cast<uint32_t*>(stagingMapped);
    }

}
