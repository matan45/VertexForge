#include "VFXRuntimeServiceImpl.hpp"
#include "../../providers/vfx/IVFXRuntimeProvider.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vfx/VFXRuntimeEvents.hpp"
#include "../../events/vfx/VFXSnapshotEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/scene/ComponentPhysicsLightEvents.hpp"
#include "../../events/physics/SocketEvents.hpp"
#include <vfx/VFXRuntimeDiagnostics.hpp>
#include <algorithm>

namespace services
{
    VFXRuntimeServiceImpl::VFXRuntimeServiceImpl(IVFXRuntimeProvider* provider)
        : vfxProvider(provider)
    {
    }

    void VFXRuntimeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::vfxruntime::CreateVFXInstanceCommand>(
            [this](const events::vfxruntime::CreateVFXInstanceCommand& cmd)
            {
                return createInstance(cmd.params);
            });

        dispatcher.registerCommandHandler<events::vfxruntime::CreateVFXChannelCommand>(
            [this](const events::vfxruntime::CreateVFXChannelCommand& cmd)
            {
                return createChannel(cmd.vfxAssetPath, cmd.particlesPerRequest);
            });

        dispatcher.registerCommandHandler<events::vfxruntime::EmitToVFXChannelCommand>(
            [this](const events::vfxruntime::EmitToVFXChannelCommand& cmd)
            {
                emitToChannel(cmd.channelId, cmd.params);
            });

        dispatcher.registerCommandHandler<events::vfxruntime::DestroyVFXInstanceCommand>(
            [this](const events::vfxruntime::DestroyVFXInstanceCommand& cmd)
            {
                destroyInstance(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::vfxruntime::SetVFXInstanceTransformCommand>(
            [this](const events::vfxruntime::SetVFXInstanceTransformCommand& cmd)
            {
                setInstanceTransform(cmd.instanceId, cmd.worldTransform);
            });

        dispatcher.registerCommandHandler<events::vfxruntime::AttachVFXInstanceToSocketCommand>(
            [this](const events::vfxruntime::AttachVFXInstanceToSocketCommand& cmd)
            {
                attachInstanceToSocket(cmd.instanceId, EntityHandle{cmd.entityHandle}, cmd.socketName);
            });

        dispatcher.registerCommandHandler<events::vfxruntime::DetachVFXInstanceCommand>(
            [this](const events::vfxruntime::DetachVFXInstanceCommand& cmd)
            {
                detachInstance(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::vfxruntime::ApplyVFXInstanceOverridesCommand>(
            [this](const events::vfxruntime::ApplyVFXInstanceOverridesCommand& cmd)
            {
                applyInstanceOverrides(cmd.instanceId, cmd.overrides);
            });

        dispatcher.registerCommandHandler<events::vfxruntime::PlayVFXInstanceCommand>(
            [this](const events::vfxruntime::PlayVFXInstanceCommand& cmd)
            {
                playInstance(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::vfxruntime::StopVFXInstanceCommand>(
            [this](const events::vfxruntime::StopVFXInstanceCommand& cmd)
            {
                stopInstance(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::vfxruntime::ResetVFXInstanceCommand>(
            [this](const events::vfxruntime::ResetVFXInstanceCommand& cmd)
            {
                resetInstance(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::vfxruntime::UpdateVFXRuntimeCommand>(
            [this](const events::vfxruntime::UpdateVFXRuntimeCommand& cmd)
            {
                update(cmd.deltaTime);
            });

        dispatcher.registerQueryHandler<events::vfxruntime::IsVFXInstancePlayingQuery>(
            [this](const events::vfxruntime::IsVFXInstancePlayingQuery& query)
            {
                return isInstancePlaying(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::vfxruntime::GetVFXBudgetStatsQuery>(
            [this](const events::vfxruntime::GetVFXBudgetStatsQuery&)
            {
                auto bs = getBudgetStats();
                events::vfxruntime::VFXBudgetStatsResult result;
                result.activeEmitters = bs.activeEmitters;
                result.maxEmitters = bs.maxEmitters;
                result.allocatedParticles = bs.allocatedParticles;
                result.maxParticles = bs.maxParticles;
                std::copy(std::begin(bs.lodCounts), std::end(bs.lodCounts), std::begin(result.lodCounts));
                result.fragmentationPercent = bs.fragmentationPercent;
                result.poolWarmSlots = bs.poolWarmSlots;
                result.poolUsedSlots = bs.poolUsedSlots;
                result.poolTotalSlots = bs.poolTotalSlots;
                result.culledEmitters = bs.culledEmitters;
                result.throttledEmitters = bs.throttledEmitters;
                result.vfxCullDistance = bs.vfxCullDistance;
                result.eventsThisFrame = bs.eventsThisFrame;
                result.rawEventsThisFrame = bs.rawEventsThisFrame;
                result.eventBudget = bs.eventBudget;
                result.eventsDropped = bs.eventsDropped;
                result.channelListeners = bs.channelListeners;
                result.channelRawRequests = bs.channelRawRequests;
                result.channelAcceptedRequests = bs.channelAcceptedRequests;
                result.channelRingDroppedRequests = bs.channelRingDroppedRequests;
                result.channelParticleDroppedRequests = bs.channelParticleDroppedRequests;
                result.channelRequestBudget = bs.channelRequestBudget;
                result.evictedInstances = bs.evictedInstances;
                result.maxLiveInstances = bs.maxLiveInstances;
                return result;
            });

        dispatcher.registerQueryHandler<events::vfxruntime::GetVFXLODConfigQuery>(
            [this](const events::vfxruntime::GetVFXLODConfigQuery&)
            {
                events::vfxruntime::VFXLODConfigResult result;
                if (vfxProvider)
                {
                    auto lc = vfxProvider->getLODConfig();
                    result.lod0Distance = lc.lod0Distance;
                    result.lod1Distance = lc.lod1Distance;
                    result.lod2Distance = lc.lod2Distance;
                    result.transitionZone = lc.transitionZone;
                }
                return result;
            });

        dispatcher.registerCommandHandler<events::vfxruntime::SetVFXLODConfigCommand>(
            [this](const events::vfxruntime::SetVFXLODConfigCommand& cmd)
            {
                if (vfxProvider)
                {
                    IVFXRuntimeProvider::LODConfig config;
                    config.lod0Distance = cmd.lod0Distance;
                    config.lod1Distance = cmd.lod1Distance;
                    config.lod2Distance = cmd.lod2Distance;
                    config.transitionZone = cmd.transitionZone;
                    vfxProvider->setLODConfig(config);
                }
            });

        // ============================================================
        // VK-1453 (Phase 4) — pre-cull state, quality tier, debug snapshots
        // ============================================================

        dispatcher.registerQueryHandler<events::vfxruntime::GetVFXCullStateQuery>(
            [this](const events::vfxruntime::GetVFXCullStateQuery&)
            {
                events::vfxruntime::VFXCullStateResult result;
                if (vfxProvider)
                {
                    const auto cs = vfxProvider->getCullState();
                    result.valid = cs.valid;
                    result.viewProj = cs.viewProj;
                    result.cameraPos = cs.cameraPos;
                    result.distanceCullEnabled = cs.distanceCullEnabled;
                    result.maxDrawDistance = cs.maxDrawDistance;
                }
                return result;
            });

        dispatcher.registerCommandHandler<events::vfxruntime::SetVFXQualityTierCommand>(
            [this](const events::vfxruntime::SetVFXQualityTierCommand& cmd)
            {
                currentTier = cmd.tier;
                if (vfxProvider)
                    vfxProvider->setQualityTier(cmd.tier);
            });

        dispatcher.registerQueryHandler<events::vfxruntime::GetVFXQualityTierQuery>(
            [this](const events::vfxruntime::GetVFXQualityTierQuery&)
            {
                return currentTier;
            });

        // VK-1503 (M4 slice-c) — scene-wide live-instance budget for the significance cap.
        dispatcher.registerCommandHandler<events::vfxruntime::SetVFXSignificanceBudgetCommand>(
            [this](const events::vfxruntime::SetVFXSignificanceBudgetCommand& cmd)
            {
                if (vfxProvider)
                    vfxProvider->setSignificanceBudget(cmd.budget);
            });

        dispatcher.registerQueryHandler<events::vfxruntime::GetVFXInstanceDebugQuery>(
            [this](const events::vfxruntime::GetVFXInstanceDebugQuery&)
            {
                events::vfxruntime::VFXInstanceDebugResult result;
                if (vfxProvider)
                {
                    const auto infos = vfxProvider->getInstanceDebugInfo();
                    result.instances.reserve(infos.size());
                    for (const auto& info : infos)
                    {
                        events::vfxruntime::VFXInstanceDebugEntry entry;
                        entry.id = info.id;
                        entry.worldPosition = info.worldPosition;
                        entry.extents = info.extents;
                        entry.inFrustum = info.inFrustum;
                        entry.lod = info.lod;
                        entry.particleCount = info.particleCount;
                        entry.priority = info.priority;
                        result.instances.push_back(entry);
                    }
                }
                return result;
            });

        dispatcher.registerQueryHandler<events::vfxruntime::GetVFXRecentWarningsQuery>(
            [](const events::vfxruntime::GetVFXRecentWarningsQuery&)
            {
                events::vfxruntime::VFXRecentWarningsResult result;
                const auto warnings = vfx::VFXRuntimeDiagnostics::instance().recent(64);
                result.warnings.reserve(warnings.size());
                for (const auto& w : warnings)
                {
                    events::vfxruntime::VFXWarningInfo info;
                    info.source = w.source;
                    info.message = w.message;
                    info.count = w.count;
                    info.lastSeq = w.lastSeq;
                    result.warnings.push_back(std::move(info));
                }
                return result;
            });

        dispatcher.registerQueryHandler<::events::vfx::snapshot::CaptureVFXSnapshotQuery>(
            [this](const ::events::vfx::snapshot::CaptureVFXSnapshotQuery& query)
                -> std::optional<::events::vfx::snapshot::VFXPlaybackSnapshot>
            {
                if (!vfxProvider) return std::nullopt;
                auto state = vfxProvider->capturePlaybackState(query.instanceId);
                if (!state) return std::nullopt;
                ::events::vfx::snapshot::VFXPlaybackSnapshot snap;
                snap.emissionTime = state->emissionTime;
                snap.spawnAccumulator = state->spawnAccumulator;
                snap.wasPlaying = state->wasPlaying;
                snap.wasActive = state->wasActive;
                return snap;
            });

        dispatcher.registerCommandHandler<::events::vfx::snapshot::SeekVFXInstanceCommand>(
            [this](const ::events::vfx::snapshot::SeekVFXInstanceCommand& cmd)
            {
                if (vfxProvider)
                    vfxProvider->seekInstance(cmd.instanceId, cmd.emissionTime, cmd.spawnAccumulator);
            });

        dispatcher.subscribe<::events::scene::SceneLoadedNotification>(
            [this](const ::events::scene::SceneLoadedNotification&)
            {
                auto& disp = ::events::EventDispatcher::instance();
                ::events::scene::GetRenderSettingsQuery query;
                auto settings = disp.query(query);
                const auto& vfxLOD = settings.vfxLOD;

                events::vfxruntime::SetVFXLODConfigCommand cmd;
                cmd.lod0Distance = vfxLOD.lod0Distance;
                cmd.lod1Distance = vfxLOD.lod1Distance;
                cmd.lod2Distance = vfxLOD.lod2Distance;
                cmd.transitionZone = vfxLOD.transitionZone;
                disp.execute(cmd);

                // VK-1460: also drive the scalability quality tier from the render
                // preset (mirrors the LOD forwarding above). Without this the
                // SetVFXQualityTierCommand handler is never invoked and currentTier
                // stays permanently High regardless of the selected preset.
                events::vfxruntime::SetVFXQualityTierCommand tierCmd;
                tierCmd.tier = settings.vfxQualityTier;
                disp.execute(tierCmd);
            });
    }

    VFXInstanceId VFXRuntimeServiceImpl::createInstance(const VFXRuntimeParams& params)
    {
        if (!vfxProvider)
            return 0;
        return vfxProvider->createInstance(params);
    }

    VFXInstanceId VFXRuntimeServiceImpl::createChannel(const std::string& path,
                                                       uint32_t particlesPerRequest)
    {
        if (!vfxProvider || path.empty())
            return 0;
        return vfxProvider->createChannel(path, particlesPerRequest);
    }

    void VFXRuntimeServiceImpl::emitToChannel(VFXInstanceId id,
                                              const VFXChannelEmitParams& params)
    {
        if (vfxProvider && id != 0)
            vfxProvider->emitToChannel(id, params);
    }

    void VFXRuntimeServiceImpl::destroyInstance(VFXInstanceId id)
    {
        socketAttachments.erase(id);
        if (vfxProvider)
            vfxProvider->destroyInstance(id);
    }

    void VFXRuntimeServiceImpl::applyInstanceOverrides(VFXInstanceId id, const VFXEmitterOverrides& overrides)
    {
        if (vfxProvider)
            vfxProvider->applyInstanceOverrides(id, overrides);
    }

    void VFXRuntimeServiceImpl::setInstanceTransform(VFXInstanceId id, const glm::mat4& worldTransform)
    {
        if (vfxProvider)
            vfxProvider->setInstanceTransform(id, worldTransform);
    }

    void VFXRuntimeServiceImpl::attachInstanceToSocket(VFXInstanceId id, EntityHandle entity,
                                                       const std::string& socketName)
    {
        if (id == 0 || !entity.isValid() || socketName.empty())
            return;
        socketAttachments[id] = SocketAttachment{entity, socketName};
    }

    void VFXRuntimeServiceImpl::detachInstance(VFXInstanceId id)
    {
        socketAttachments.erase(id);
    }

    void VFXRuntimeServiceImpl::updateSocketAttachments()
    {
        if (socketAttachments.empty() || !vfxProvider)
            return;

        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto it = socketAttachments.begin(); it != socketAttachments.end();)
        {
            const VFXInstanceId instanceId = it->first;
            const SocketAttachment& attach = it->second;

            // Auto-clear when the entity is gone or the socket no longer exists;
            // leave the instance wherever it was last placed.
            ::events::socket::HasSocketQuery hasQuery;
            hasQuery.entity = attach.entity;
            hasQuery.socketName = attach.socketName;
            if (!dispatcher.query(hasQuery))
            {
                it = socketAttachments.erase(it);
                continue;
            }

            ::events::socket::GetSocketWorldTransformQuery xformQuery;
            xformQuery.parentEntity = attach.entity;
            xformQuery.socketName = attach.socketName;
            vfxProvider->setInstanceTransform(instanceId, dispatcher.query(xformQuery));
            ++it;
        }
    }

    void VFXRuntimeServiceImpl::playInstance(VFXInstanceId id)
    {
        if (vfxProvider)
            vfxProvider->playInstance(id);
    }

    void VFXRuntimeServiceImpl::stopInstance(VFXInstanceId id)
    {
        if (vfxProvider)
            vfxProvider->stopInstance(id);
    }

    void VFXRuntimeServiceImpl::resetInstance(VFXInstanceId id)
    {
        if (vfxProvider)
            vfxProvider->resetInstance(id);
    }

    void VFXRuntimeServiceImpl::update(float deltaTime)
    {
        // Resolve socket-attached instances to their socket's current world
        // transform before the provider advances instance simulation.
        updateSocketAttachments();
        if (vfxProvider)
            vfxProvider->update(deltaTime);
    }

    bool VFXRuntimeServiceImpl::isInstancePlaying(VFXInstanceId id) const
    {
        return vfxProvider ? vfxProvider->isInstancePlaying(id) : false;
    }

    VFXRuntimeServiceImpl::BudgetStats VFXRuntimeServiceImpl::getBudgetStats() const
    {
        BudgetStats stats{};
        if (vfxProvider)
        {
            auto ps = vfxProvider->getBudgetStats();
            stats.activeEmitters = ps.activeEmitters;
            stats.maxEmitters = ps.maxEmitters;
            stats.allocatedParticles = ps.allocatedParticles;
            stats.maxParticles = ps.maxParticles;
            std::copy(std::begin(ps.lodCounts), std::end(ps.lodCounts), std::begin(stats.lodCounts));
            stats.fragmentationPercent = ps.fragmentationPercent;
            stats.poolWarmSlots = ps.poolWarmSlots;
            stats.poolUsedSlots = ps.poolUsedSlots;
            stats.poolTotalSlots = ps.poolTotalSlots;
            stats.culledEmitters = ps.culledEmitters;
            stats.throttledEmitters = ps.throttledEmitters;
            stats.vfxCullDistance = ps.vfxCullDistance;
            stats.eventsThisFrame = ps.eventsThisFrame;
            stats.rawEventsThisFrame = ps.rawEventsThisFrame;
            stats.eventBudget = ps.eventBudget;
            stats.eventsDropped = ps.eventsDropped;
            stats.channelListeners = ps.channelListeners;
            stats.channelRawRequests = ps.channelRawRequests;
            stats.channelAcceptedRequests = ps.channelAcceptedRequests;
            stats.channelRingDroppedRequests = ps.channelRingDroppedRequests;
            stats.channelParticleDroppedRequests = ps.channelParticleDroppedRequests;
            stats.channelRequestBudget = ps.channelRequestBudget;
            stats.evictedInstances = ps.evictedInstances;
            stats.maxLiveInstances = ps.maxLiveInstances;
        }
        return stats;
    }
}
