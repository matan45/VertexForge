#include "GPUVFXBufferManager.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "print/Log.hpp"

namespace render::vfx
{
    void GPUVFXBufferManager::updateEmitterConfig(uint32_t emitterIndex, const GPUEmitterConfig& config)
    {
        if (!initialized || emitterIndex >= maxEmitters || !configMapped)
        {
            if (initialized && emitterIndex >= maxEmitters)
                vfLogWarning("GPUVFXBufferManager::updateEmitterConfig: invalid emitterIndex {}", emitterIndex);
            return;
        }

        auto* configs = static_cast<GPUEmitterConfig*>(configMapped);
        configs[emitterIndex] = config;
    }

    void GPUVFXBufferManager::updateEmitterState(uint32_t emitterIndex, const GPUEmitterState& state)
    {
        if (!initialized || emitterIndex >= maxEmitters || !stateStagingMapped[currentFrameIndex])
        {
            if (initialized && emitterIndex >= maxEmitters)
                vfLogWarning("GPUVFXBufferManager::updateEmitterState: invalid emitterIndex {}", emitterIndex);
            return;
        }

        auto* states = static_cast<GPUEmitterState*>(stateStagingMapped[currentFrameIndex]);
        states[emitterIndex] = state;
    }

    void GPUVFXBufferManager::updateEmitterLUT(uint32_t emitterIndex, const std::vector<glm::vec4>& lutData)
    {
        if (!initialized || emitterIndex >= maxEmitters || !lutMapped)
        {
            if (initialized && emitterIndex >= maxEmitters)
                vfLogWarning("GPUVFXBufferManager::updateEmitterLUT: invalid emitterIndex {}", emitterIndex);
            return;
        }

        constexpr uint32_t entriesPerEmitter = GPUVFXConstants::LUT_CHANNELS * GPUVFXConstants::LUT_RESOLUTION;
        uint32_t offset = emitterIndex * entriesPerEmitter;
        uint32_t copyCount = std::min(static_cast<uint32_t>(lutData.size()), entriesPerEmitter);

        auto* dst = static_cast<glm::vec4*>(lutMapped) + offset;
        std::memcpy(dst, lutData.data(), copyCount * sizeof(glm::vec4));
    }

    void GPUVFXBufferManager::updateSceneColliders(const std::vector<GPUCollider>& colliders, uint32_t count)
    {
        if (!initialized || !colliderMapped)
        {
            return;
        }

        uint32_t copyCount = std::min(count, GPUVFXConstants::MAX_SCENE_COLLIDERS);
        if (copyCount > 0)
        {
            std::memcpy(colliderMapped, colliders.data(), copyCount * sizeof(GPUCollider));
        }

        if (copyCount < GPUVFXConstants::MAX_SCENE_COLLIDERS)
        {
            auto* dst = static_cast<uint8_t*>(colliderMapped) + copyCount * sizeof(GPUCollider);
            std::memset(dst, 0, (GPUVFXConstants::MAX_SCENE_COLLIDERS - copyCount) * sizeof(GPUCollider));
        }
    }

    void GPUVFXBufferManager::updateTerrainHeightfield(const GPUTerrainHeightfield& header,
                                                        const float* heights, uint32_t heightCount)
    {
        if (!initialized || !terrainMapped)
        {
            return;
        }

        std::memcpy(terrainMapped, &header, sizeof(GPUTerrainHeightfield));

        if (heights && heightCount > 0)
        {
            uint32_t maxHeights = (GPUVFXConstants::MAX_TERRAIN_HEIGHTFIELD_BYTES -
                                   static_cast<uint32_t>(sizeof(GPUTerrainHeightfield))) / sizeof(float);
            uint32_t copyCount = std::min(heightCount, maxHeights);
            auto* dst = static_cast<uint8_t*>(terrainMapped) + sizeof(GPUTerrainHeightfield);
            std::memcpy(dst, heights, copyCount * sizeof(float));
        }
    }

    void GPUVFXBufferManager::clearTerrainHeightfield()
    {
        if (!initialized || !terrainMapped)
        {
            return;
        }

        auto* header = static_cast<GPUTerrainHeightfield*>(terrainMapped);
        header->enabled = 0;
    }

    void GPUVFXBufferManager::resetActiveCount(vk::CommandBuffer cmd, uint32_t emitterIndex)
    {
        if (!initialized || emitterIndex >= maxEmitters)
        {
            return;
        }

        vk::DeviceSize stateOffset = static_cast<vk::DeviceSize>(emitterIndex) * sizeof(GPUEmitterState);
        vk::DeviceSize activeCountOffset = stateOffset + offsetof(GPUEmitterState, activeCount);

        cmd.fillBuffer(stateBuffer, activeCountOffset, sizeof(uint32_t), 0);

        vk::DeviceSize spawnCounterOffset = stateOffset + offsetof(GPUEmitterState, spawnCounter);
        cmd.fillBuffer(stateBuffer, spawnCounterOffset, sizeof(uint32_t), 0);
    }

    void GPUVFXBufferManager::resetAllActiveCounts(vk::CommandBuffer cmd)
    {
        if (!initialized)
        {
            return;
        }

        for (uint32_t i = 0; i < maxEmitters; ++i)
        {
            if (emitterSlots[i])
            {
                resetActiveCount(cmd, i);
            }
        }
    }

    void GPUVFXBufferManager::uploadStateBuffer(vk::CommandBuffer cmd)
    {
        if (!initialized || !stateStagingBuffers[currentFrameIndex] || !stateBuffer)
        {
            return;
        }

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = getStateBufferSize();

        cmd.copyBuffer(stateStagingBuffers[currentFrameIndex], stateBuffer, copyRegion);
    }

    void GPUVFXBufferManager::clearDrawCommands(vk::CommandBuffer cmd)
    {
        if (!initialized || !drawCommandBuffer)
        {
            return;
        }

        cmd.fillBuffer(drawCommandBuffer, 0, getDrawCommandBufferSize(), 0);
    }

    void GPUVFXBufferManager::clearParticleBufferIfNeeded(vk::CommandBuffer cmd)
    {
        if (!initialized || !particleBuffer || particleBufferCleared)
        {
            return;
        }

        cmd.fillBuffer(particleBuffer, 0, getParticleBufferSize(), 0);
        particleBufferCleared = true;
    }

    void GPUVFXBufferManager::clearRibbonHead(vk::CommandBuffer cmd, uint32_t emitterIndex)
    {
        if (!initialized || emitterIndex >= maxEmitters || !ribbonHeadBuffer)
        {
            return;
        }

        vk::DeviceSize offset = static_cast<vk::DeviceSize>(emitterIndex) * sizeof(uint32_t);
        cmd.fillBuffer(ribbonHeadBuffer, offset, sizeof(uint32_t), 0);
    }

    void GPUVFXBufferManager::clearEventBuffer(vk::CommandBuffer cmd)
    {
        if (!initialized || !eventBuffer)
        {
            return;
        }

        cmd.fillBuffer(eventBuffer, 0, sizeof(uint32_t), 0);
    }

    void GPUVFXBufferManager::copyEventBufferToReadback(vk::CommandBuffer cmd)
    {
        if (!initialized || !eventBuffer || !eventReadbackBuffers[currentFrameIndex])
        {
            return;
        }

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = getEventBufferSize();
        cmd.copyBuffer(eventBuffer, eventReadbackBuffers[currentFrameIndex], copyRegion);
    }

    std::vector<GPUVFXEvent> GPUVFXBufferManager::readbackEvents(uint32_t& outEventCount)
    {
        uint32_t readIndex = (currentFrameIndex + core::MAX_FRAMES_IN_FLIGHT - 1)
                             % core::MAX_FRAMES_IN_FLIGHT;

        if (!eventReadbackMapped[readIndex])
        {
            outEventCount = 0;
            return {};
        }

        auto* data = static_cast<const uint8_t*>(eventReadbackMapped[readIndex]);
        uint32_t eventCount = *reinterpret_cast<const uint32_t*>(data);
        eventCount = std::min(eventCount, GPUVFXConstants::MAX_VFX_EVENTS_PER_FRAME);
        outEventCount = eventCount;

        if (eventCount == 0)
        {
            return {};
        }

        // std430: events[] starts at offset 16 due to struct alignment padding
        constexpr size_t EVENT_DATA_OFFSET = 16;
        std::vector<GPUVFXEvent> events(eventCount);
        std::memcpy(events.data(), data + EVENT_DATA_OFFSET, eventCount * sizeof(GPUVFXEvent));
        return events;
    }

    GPUVFXBufferManager::EmitterAllocation GPUVFXBufferManager::allocateEmitter(uint32_t particleCount)
    {
        EmitterAllocation allocation{};
        allocation.emitterIndex = UINT32_MAX;

        if (!initialized)
        {
            vfLogError("GPUVFXBufferManager::allocateEmitter: Not initialized");
            return allocation;
        }

        uint32_t freeSlot = UINT32_MAX;
        for (uint32_t i = 0; i < maxEmitters; ++i)
        {
            if (!emitterSlots[i])
            {
                freeSlot = i;
                break;
            }
        }

        if (freeSlot == UINT32_MAX)
        {
            vfLogError("GPUVFXBufferManager::allocateEmitter: No free emitter slots");
            return allocation;
        }

        // Initialize free list if empty (first allocation after init)
        if (freeList.empty() && allocatedParticleCount == 0)
        {
            freeList.push_back({0, maxParticles});
        }

        // First-fit search in free list
        uint32_t bestIdx = UINT32_MAX;
        for (uint32_t i = 0; i < static_cast<uint32_t>(freeList.size()); ++i)
        {
            if (freeList[i].count >= particleCount)
            {
                bestIdx = i;
                break;
            }
        }

        if (bestIdx == UINT32_MAX)
        {
            vfLogError("GPUVFXBufferManager::allocateEmitter: No contiguous particle block of size {} available",
                        particleCount);
            return allocation;
        }

        uint32_t particleOffset = freeList[bestIdx].offset;

        // Split or consume the free block
        if (freeList[bestIdx].count == particleCount)
        {
            freeList.erase(freeList.begin() + bestIdx);
        }
        else
        {
            freeList[bestIdx].offset += particleCount;
            freeList[bestIdx].count -= particleCount;
        }

        allocation.emitterIndex = freeSlot;
        allocation.particleOffset = particleOffset;
        allocation.particleCount = particleCount;

        emitterSlots[freeSlot] = true;
        emitterParticleOffsets[freeSlot] = particleOffset;
        emitterParticleCounts[freeSlot] = particleCount;
        allocatedParticleCount += particleCount;
        activeEmitterCount++;

        return allocation;
    }

    void GPUVFXBufferManager::freeEmitter(uint32_t emitterIndex)
    {
        if (!initialized || emitterIndex >= maxEmitters || !emitterSlots[emitterIndex])
        {
            return;
        }

        uint32_t offset = emitterParticleOffsets[emitterIndex];
        uint32_t count = emitterParticleCounts[emitterIndex];

        emitterSlots[emitterIndex] = false;
        allocatedParticleCount -= count;
        activeEmitterCount--;

        // Insert free block in sorted order and coalesce adjacent blocks
        FreeBlock newBlock{offset, count};

        // Find insertion point (sorted by offset)
        auto insertIt = freeList.begin();
        while (insertIt != freeList.end() && insertIt->offset < offset)
        {
            ++insertIt;
        }

        insertIt = freeList.insert(insertIt, newBlock);

        // Coalesce with next block
        auto nextIt = insertIt + 1;
        if (nextIt != freeList.end() &&
            insertIt->offset + insertIt->count == nextIt->offset)
        {
            insertIt->count += nextIt->count;
            freeList.erase(nextIt);
        }

        // Coalesce with previous block
        if (insertIt != freeList.begin())
        {
            auto prevIt = insertIt - 1;
            if (prevIt->offset + prevIt->count == insertIt->offset)
            {
                prevIt->count += insertIt->count;
                freeList.erase(insertIt);
            }
        }
    }

    float GPUVFXBufferManager::getFragmentationPercent() const
    {
        if (freeList.empty() || allocatedParticleCount == 0)
        {
            return 0.0f;
        }

        uint32_t totalFree = 0;
        uint32_t largestFree = 0;
        for (const auto& block : freeList)
        {
            totalFree += block.count;
            if (block.count > largestFree)
            {
                largestFree = block.count;
            }
        }

        if (totalFree == 0)
        {
            return 0.0f;
        }

        // Fragmentation = 1 - (largestFreeBlock / totalFree)
        return (1.0f - static_cast<float>(largestFree) / static_cast<float>(totalFree)) * 100.0f;
    }
}
