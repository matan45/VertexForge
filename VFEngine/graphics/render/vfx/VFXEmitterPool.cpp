#include "VFXEmitterPool.hpp"
#include "print/Log.hpp"

namespace render::vfx
{
    VFXEmitterPool::VFXEmitterPool(GPUVFXBufferManager& bufferManager,
                                     uint32_t warmSlots,
                                     uint32_t particlesPerSlot)
        : bufferManager(bufferManager)
        , targetWarmSlots(warmSlots)
        , particlesPerSlot(particlesPerSlot)
    {
    }

    void VFXEmitterPool::warmUp()
    {
        slots.clear();
        slots.reserve(targetWarmSlots);

        for (uint32_t i = 0; i < targetWarmSlots; ++i)
        {
            auto allocation = bufferManager.allocateEmitter(particlesPerSlot);
            if (allocation.emitterIndex == UINT32_MAX)
            {
                vfLogWarning("VFXEmitterPool: Could only warm {} of {} requested slots", i, targetWarmSlots);
                break;
            }

            PoolSlot slot;
            slot.emitterIndex = allocation.emitterIndex;
            slot.particleOffset = allocation.particleOffset;
            slot.particleCount = allocation.particleCount;
            slot.inUse = false;
            slots.push_back(slot);
        }

        vfLogInfo("VFXEmitterPool: Warmed {} slots ({} particles each)", slots.size(), particlesPerSlot);
    }

    VFXEmitterPool::AcquireResult VFXEmitterPool::acquire()
    {
        AcquireResult result;

        // Search for an available warm slot
        for (auto& slot : slots)
        {
            if (!slot.inUse)
            {
                slot.inUse = true;
                result.emitterIndex = slot.emitterIndex;
                result.particleOffset = slot.particleOffset;
                result.particleCount = slot.particleCount;
                return result;
            }
        }

        // Fallback: allocate directly from buffer manager
        auto allocation = bufferManager.allocateEmitter(particlesPerSlot);
        if (allocation.emitterIndex != UINT32_MAX)
        {
            PoolSlot slot;
            slot.emitterIndex = allocation.emitterIndex;
            slot.particleOffset = allocation.particleOffset;
            slot.particleCount = allocation.particleCount;
            slot.inUse = true;
            slots.push_back(slot);

            result.emitterIndex = allocation.emitterIndex;
            result.particleOffset = allocation.particleOffset;
            result.particleCount = allocation.particleCount;
        }

        return result;
    }

    void VFXEmitterPool::release(uint32_t emitterIndex)
    {
        for (auto& slot : slots)
        {
            if (slot.emitterIndex == emitterIndex && slot.inUse)
            {
                slot.inUse = false;
                return;
            }
        }
    }

    void VFXEmitterPool::reset()
    {
        // Free all slots back to buffer manager
        for (auto& slot : slots)
        {
            bufferManager.freeEmitter(slot.emitterIndex);
        }
        slots.clear();
    }

    uint32_t VFXEmitterPool::getWarmSlotCount() const
    {
        uint32_t count = 0;
        for (const auto& slot : slots)
        {
            if (!slot.inUse)
                count++;
        }
        return count;
    }

    uint32_t VFXEmitterPool::getUsedSlotCount() const
    {
        uint32_t count = 0;
        for (const auto& slot : slots)
        {
            if (slot.inUse)
                count++;
        }
        return count;
    }
}
