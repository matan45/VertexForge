#pragma once
#include "../../data/VFXTypes.hpp"

namespace services
{
    class IVFXRuntimeProvider;

    class VFXRuntimeServiceImpl
    {
    private:
        IVFXRuntimeProvider* vfxProvider = nullptr;

    public:
        explicit VFXRuntimeServiceImpl(IVFXRuntimeProvider* provider);
        ~VFXRuntimeServiceImpl() = default;

        void registerEventHandlers();

        VFXInstanceId createInstance(const VFXRuntimeParams& params);
        void destroyInstance(VFXInstanceId id);
        void setInstanceTransform(VFXInstanceId id, const glm::mat4& worldTransform);
        void playInstance(VFXInstanceId id);
        void stopInstance(VFXInstanceId id);
        void resetInstance(VFXInstanceId id);
        void update(float deltaTime);

        bool isInstancePlaying(VFXInstanceId id) const;

        struct BudgetStats
        {
            uint32_t activeEmitters = 0;
            uint32_t maxEmitters = 0;
            uint32_t allocatedParticles = 0;
            uint32_t maxParticles = 0;
            uint32_t lodCounts[4] = {0, 0, 0, 0};
            float fragmentationPercent = 0.0f;
            uint32_t poolWarmSlots = 0;
            uint32_t poolUsedSlots = 0;
            uint32_t poolTotalSlots = 0;
        };
        BudgetStats getBudgetStats() const;
    };
}
