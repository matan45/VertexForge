#include "VFXRuntimeServiceImpl.hpp"
#include "../providers/IVFXRuntimeProvider.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/VFXRuntimeEvents.hpp"

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
}
