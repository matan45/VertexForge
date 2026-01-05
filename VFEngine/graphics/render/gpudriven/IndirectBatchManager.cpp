#include "IndirectBatchManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
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

    vk::DeviceSize IndirectBatchManager::calculateRequiredMemory(uint32_t batchCount,
                                                                   uint32_t commandsPerBatch,
                                                                   uint32_t shaderGroupCount)
    {
        uint32_t commandsPerSection = commandsPerBatch / shaderGroupCount;
        uint32_t totalSections = batchCount * shaderGroupCount;

        // Use 64-bit arithmetic to prevent overflow in size calculations
        vk::DeviceSize totalCommands = static_cast<vk::DeviceSize>(totalSections) * commandsPerSection;
        vk::DeviceSize drawCommandSize = totalCommands * sizeof(MeshTasksIndirectCommand);
        vk::DeviceSize drawCountSize = static_cast<vk::DeviceSize>(totalSections) * sizeof(BatchDrawStats);
        vk::DeviceSize perDrawDataSize = totalCommands * sizeof(PerDrawData);
        vk::DeviceSize stagingSize = drawCountSize;

        return drawCommandSize + drawCountSize + perDrawDataSize + stagingSize;
    }

    bool IndirectBatchManager::initWithAutoConfig()
    {
        if (initialized) {
            loggerWarning("IndirectBatchManager already initialized");
            return true;
        }

        // Query available VRAM
        auto memInfo = device.getDeviceMemoryInfo();
        vk::DeviceSize availableVRAM = memInfo.deviceLocalHeapSize;

        loggerInfo("IndirectBatchManager: Detected {} MB device-local VRAM{}",
                   availableVRAM / (1024 * 1024),
                   memInfo.hasUnifiedMemory ? " (unified memory)" : "");

        // Select configuration based on available VRAM
        // Reserve ~25% of VRAM budget for indirect buffers (rest for textures, meshes, etc.)
        vk::DeviceSize budgetForIndirect = availableVRAM / 4;

        uint32_t selectedBatchCount = DEFAULT_BATCH_COUNT;
        uint32_t selectedCommands = MAX_DRAW_COMMANDS;
        uint32_t selectedGroups = MAX_SHADER_GROUPS;

        // Device tiers based on VRAM
        constexpr vk::DeviceSize GB = 1024ull * 1024ull * 1024ull;

        if (availableVRAM < 4 * GB) {
            // Low-end: < 4GB VRAM
            selectedBatchCount = 2;
            selectedCommands = 100000;
            selectedGroups = 8;
            loggerInfo("IndirectBatchManager: Using LOW-END configuration (< 4GB VRAM)");
        } else if (availableVRAM < 8 * GB) {
            // Mid-range: 4-8GB VRAM
            selectedBatchCount = 4;
            selectedCommands = 300000;
            selectedGroups = 16;
            loggerInfo("IndirectBatchManager: Using MID-RANGE configuration (4-8GB VRAM)");
        } else {
            // High-end: >= 8GB VRAM
            selectedBatchCount = DEFAULT_BATCH_COUNT;
            selectedCommands = MAX_DRAW_COMMANDS;
            selectedGroups = MAX_SHADER_GROUPS;
            loggerInfo("IndirectBatchManager: Using HIGH-END configuration (>= 8GB VRAM)");
        }

        // Verify the selected config fits in budget
        vk::DeviceSize requiredMemory = calculateRequiredMemory(selectedBatchCount, selectedCommands, selectedGroups);
        if (requiredMemory > budgetForIndirect) {
            loggerWarning("IndirectBatchManager: Selected config requires {} MB but budget is {} MB, reducing further",
                         requiredMemory / (1024 * 1024), budgetForIndirect / (1024 * 1024));

            // Scale down commands proportionally
            float scale = static_cast<float>(budgetForIndirect) / static_cast<float>(requiredMemory);
            selectedCommands = static_cast<uint32_t>(selectedCommands * scale * 0.9f); // 10% safety margin
            selectedCommands = std::max(selectedCommands, 10000u); // Minimum viable
        }

        loggerInfo("IndirectBatchManager: Allocating {} MB for indirect buffers",
                   calculateRequiredMemory(selectedBatchCount, selectedCommands, selectedGroups) / (1024 * 1024));

        return init(selectedBatchCount, selectedCommands, selectedGroups);
    }

    bool IndirectBatchManager::init(uint32_t batchCount, uint32_t commandsPerBatch, uint32_t shaderGroupCount)
    {
        if (initialized) {
            loggerWarning("IndirectBatchManager already initialized");
            return true;
        }

        // Validate batch count
        if (batchCount == 0 || batchCount > MAX_BATCH_COUNT) {
            loggerError("Invalid batch count: {}. Must be 1-{}", batchCount, MAX_BATCH_COUNT);
            batchCount = DEFAULT_BATCH_COUNT;
        }

        // Validate shader group count
        if (shaderGroupCount == 0 || shaderGroupCount > MAX_SHADER_GROUPS) {
            loggerError("Invalid shader group count: {}. Must be 1-{}", shaderGroupCount, MAX_SHADER_GROUPS);
            shaderGroupCount = MAX_SHADER_GROUPS;
        }

        this->batchCount = batchCount;
        this->commandsPerBatch = commandsPerBatch;
        this->shaderGroupCount = shaderGroupCount;
        // Each (batch, shaderGroup) section gets equal share of commands
        this->commandsPerSection = commandsPerBatch / shaderGroupCount;

        if (!createBuffers()) {
            loggerError("IndirectBatchManager: Failed to allocate GPU buffers");
            return false;
        }

        initialized = true;

        loggerInfo("IndirectBatchManager initialized: {} batches x {} shader groups x {} commands/section = {} total capacity",
                   this->batchCount, this->shaderGroupCount, this->commandsPerSection, getTotalCapacity());
        loggerInfo("  Sections (batch*group pairs): {}", getSectionCount());
        loggerInfo("  Draw command buffer: {:.1f} MB", getCombinedDrawCommandBufferSize() / (1024.0f * 1024.0f));
        loggerInfo("  Draw count buffer: {:.1f} KB", getCombinedDrawCountBufferSize() / 1024.0f);
        loggerInfo("  Per-draw data buffer: {:.1f} MB", getCombinedPerDrawDataBufferSize() / (1024.0f * 1024.0f));
        loggerInfo("  Total GPU memory: {:.1f} MB",
                   (getCombinedDrawCommandBufferSize() + getCombinedDrawCountBufferSize() +
                    getCombinedPerDrawDataBufferSize()) / (1024.0f * 1024.0f));

        return true;
    }

    void IndirectBatchManager::cleanup()
    {
        if (!initialized) return;

        device.getLogicalDevice().waitIdle();
        destroyBuffers();
        initialized = false;

        loggerInfo("IndirectBatchManager cleaned up");
    }

    bool IndirectBatchManager::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        try {
            // Combined draw command buffer - all batches contiguous
            // Written by compute shader, read by vkCmdDrawIndexedIndirectCount
            {
                core::BufferInfoRequest request(logicalDevice, physicalDevice);
                request.size = getCombinedDrawCommandBufferSize();
                request.usage = vk::BufferUsageFlagBits::eStorageBuffer |      // Compute shader writes
                               vk::BufferUsageFlagBits::eIndirectBuffer |       // Indirect draw reads
                               vk::BufferUsageFlagBits::eTransferDst;           // Clear/reset
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, combinedDrawCommandBuffer, combinedDrawCommandMemory);
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
                core::BufferUtilities::createBuffer(request, combinedDrawCountBuffer, combinedDrawCountMemory);
            }

            // Combined per-draw data buffer - all batches contiguous
            // Written by compute shader, read by vertex/fragment shaders
            {
                core::BufferInfoRequest request(logicalDevice, physicalDevice);
                request.size = getCombinedPerDrawDataBufferSize();
                request.usage = vk::BufferUsageFlagBits::eStorageBuffer |       // Compute writes, VS/FS reads
                               vk::BufferUsageFlagBits::eTransferDst;           // Clear if needed
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, combinedPerDrawDataBuffer, combinedPerDrawDataMemory);
            }

            // Staging buffer for count reset and readback (needs to hold all batch stats)
            {
                core::BufferInfoRequest request(logicalDevice, physicalDevice);
                request.size = getCombinedDrawCountBufferSize();
                request.usage = vk::BufferUsageFlagBits::eTransferSrc |
                               vk::BufferUsageFlagBits::eTransferDst;
                request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;
                core::BufferUtilities::createBuffer(request, stagingBuffer, stagingMemory);

                stagingMapped = logicalDevice.mapMemory(
                    stagingMemory, 0, getCombinedDrawCountBufferSize(), vk::MemoryMapFlags{}
                );
                // Initialize all stats to 0
                std::memset(stagingMapped, 0, getCombinedDrawCountBufferSize());
            }

            return true;

        } catch (const vk::OutOfDeviceMemoryError& e) {
            loggerError("IndirectBatchManager: Out of device memory - {}", e.what());
            destroyBuffers();
            return false;
        } catch (const vk::OutOfHostMemoryError& e) {
            loggerError("IndirectBatchManager: Out of host memory - {}", e.what());
            destroyBuffers();
            return false;
        } catch (const vk::SystemError& e) {
            loggerError("IndirectBatchManager: Vulkan error during buffer creation - {}", e.what());
            destroyBuffers();
            return false;
        }
    }

    void IndirectBatchManager::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (stagingMapped) {
            logicalDevice.unmapMemory(stagingMemory);
            stagingMapped = nullptr;
        }

        core::BufferUtilities::destroyBuffer(logicalDevice, stagingBuffer, stagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, combinedPerDrawDataBuffer, combinedPerDrawDataMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, combinedDrawCountBuffer, combinedDrawCountMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, combinedDrawCommandBuffer, combinedDrawCommandMemory);
    }

    void IndirectBatchManager::resetAllBatches(vk::CommandBuffer cmd)
    {
        std::memset(stagingMapped, 0, getCombinedDrawCountBufferSize());
        
        vk::BufferCopy copyRegion;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = getCombinedDrawCountBufferSize();
        cmd.copyBuffer(stagingBuffer, combinedDrawCountBuffer, copyRegion);
        
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
        std::array<vk::BufferMemoryBarrier, 3> barriers;

        // Draw command buffer: Compute shader write → Indirect command read
        barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[0].dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].buffer = combinedDrawCommandBuffer;
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;

        // Draw count buffer: Compute shader write → Indirect command read
        barriers[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[1].dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].buffer = combinedDrawCountBuffer;
        barriers[1].offset = 0;
        barriers[1].size = VK_WHOLE_SIZE;

        // Per-draw data buffer: Compute shader write → Task/Mesh shader read
        barriers[2].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barriers[2].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[2].buffer = combinedPerDrawDataBuffer;
        barriers[2].offset = 0;
        barriers[2].size = VK_WHOLE_SIZE;

        // Mesh shader path: ComputeShader → DrawIndirect + TaskShader
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eTaskShaderEXT,
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
        // Returns stats for all sections (batchCount * shaderGroupCount)

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

        // Read from staging into vector - one entry per section (batch * shaderGroup)
        std::vector<BatchDrawStats> stats(getSectionCount());
        std::memcpy(stats.data(), stagingMapped, getCombinedDrawCountBufferSize());

        return stats;
    }

    GPUDrivenStats IndirectBatchManager::readBackAggregatedStats()
    {
        auto sectionStats = readBackAllStats();

        GPUDrivenStats aggregated{};
        // One draw call per active (batch, shaderGroup) section
        aggregated.drawCalls = getSectionCount();

        for (const auto& section : sectionStats) {
            aggregated.visibleObjects += section.drawCount;
            aggregated.objectsLOD0 += section.lodCount0;
            aggregated.objectsLOD1 += section.lodCount1;
            aggregated.objectsLOD2 += section.lodCount2;
            aggregated.objectsLOD3 += section.lodCount3;
            aggregated.culledByFrustum += section.culledByFrustum;
            aggregated.culledByOcclusion += section.culledByOcclusion;
        }

        return aggregated;
    }

}
