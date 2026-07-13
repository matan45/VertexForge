#pragma once
#include "../../data/VFXTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include <vfx/VFXScalability.hpp>
#include <string>
#include <unordered_map>

namespace services
{
    class IVFXRuntimeProvider;

    class VFXRuntimeServiceImpl
    {
    private:
        IVFXRuntimeProvider* vfxProvider = nullptr;

        // VK-1453 (Phase 4) — cached global VFX quality tier. SetVFXQualityTierCommand
        // updates this (and forwards to the provider); GetVFXQualityTierQuery returns it.
        vfx::VFXQualityTier currentTier = vfx::VFXQualityTier::High;

        // VFX instances attached to entity sockets. Each frame, before the
        // provider updates instance transforms, the attached instance's world
        // transform is resolved from the socket and pushed down. Entries are
        // erased when the socket can no longer be resolved or the instance is
        // destroyed/detached.
        struct SocketAttachment
        {
            EntityHandle entity;
            std::string socketName;
        };
        std::unordered_map<VFXInstanceId, SocketAttachment> socketAttachments;

        void updateSocketAttachments();

    public:
        explicit VFXRuntimeServiceImpl(IVFXRuntimeProvider* provider);
        ~VFXRuntimeServiceImpl() = default;

        void registerEventHandlers();

        VFXInstanceId createInstance(const VFXRuntimeParams& params);
        VFXInstanceId createChannel(const std::string& path, uint32_t particlesPerRequest);
        void emitToChannel(VFXInstanceId id, const VFXChannelEmitParams& params);
        void destroyInstance(VFXInstanceId id);
        void applyInstanceOverrides(VFXInstanceId id, const VFXEmitterOverrides& overrides);
        void setInstanceTransform(VFXInstanceId id, const glm::mat4& worldTransform);
        void attachInstanceToSocket(VFXInstanceId id, EntityHandle entity, const std::string& socketName);
        void detachInstance(VFXInstanceId id);
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
            // VK-1453 (Phase 4) — mirror the provider's new cull/throttle telemetry.
            uint32_t culledEmitters = 0;
            uint32_t throttledEmitters = 0;
            float vfxCullDistance = 0.0f;
            uint32_t eventsThisFrame = 0;
            uint32_t rawEventsThisFrame = 0;
            uint32_t eventBudget = 0;
            bool eventsDropped = false;
            uint32_t channelListeners = 0;
            uint32_t channelRawRequests = 0;
            uint32_t channelAcceptedRequests = 0;
            uint32_t channelRingDroppedRequests = 0;
            uint32_t channelParticleDroppedRequests = 0;
            uint32_t channelRequestBudget = 0;
        };
        BudgetStats getBudgetStats() const;
    };
}
