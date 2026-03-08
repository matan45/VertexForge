#pragma once

#include "GPUVFXTypes.hpp"
#include "GPUVFXBufferManager.hpp"
#include <vector>
#include <cstdint>

namespace render::vfx
{
    class VFXEmitterPool
    {
    public:
        struct PoolSlot
        {
            uint32_t emitterIndex = UINT32_MAX;
            uint32_t particleOffset = 0;
            uint32_t particleCount = 0;
            bool inUse = false;
        };

        explicit VFXEmitterPool(GPUVFXBufferManager& bufferManager,
                                 uint32_t warmSlots = 32,
                                 uint32_t particlesPerSlot = GPUVFXConstants::DEFAULT_PARTICLES_PER_EMITTER);
        ~VFXEmitterPool() = default;

        VFXEmitterPool(const VFXEmitterPool&) = delete;
        VFXEmitterPool& operator=(const VFXEmitterPool&) = delete;

        void warmUp();

        struct AcquireResult
        {
            uint32_t emitterIndex = UINT32_MAX;
            uint32_t particleOffset = 0;
            uint32_t particleCount = 0;
            bool valid() const { return emitterIndex != UINT32_MAX; }
        };

        AcquireResult acquire();
        void release(uint32_t emitterIndex);

        void reset();

        uint32_t getWarmSlotCount() const;
        uint32_t getUsedSlotCount() const;
        uint32_t getTotalSlotCount() const { return static_cast<uint32_t>(slots.size()); }

    private:
        GPUVFXBufferManager& bufferManager;
        std::vector<PoolSlot> slots;
        uint32_t targetWarmSlots;
        uint32_t particlesPerSlot;
    };
}
