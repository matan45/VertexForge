#include "VFXRuntimeServiceImpl.hpp"
#include "../../providers/vfx/IVFXRuntimeProvider.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vfx/VFXRuntimeEvents.hpp"
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
    }

    VFXInstanceId VFXRuntimeServiceImpl::createInstance(const VFXRuntimeParams& params)
    {
        if (!vfxProvider)
            return 0;
        return vfxProvider->createInstance(params);
    }

    void VFXRuntimeServiceImpl::destroyInstance(VFXInstanceId id)
    {
        if (vfxProvider)
            vfxProvider->destroyInstance(id);
    }

    void VFXRuntimeServiceImpl::setInstanceTransform(VFXInstanceId id, const glm::mat4& worldTransform)
    {
        if (vfxProvider)
            vfxProvider->setInstanceTransform(id, worldTransform);
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
        }
        return stats;
    }
}
