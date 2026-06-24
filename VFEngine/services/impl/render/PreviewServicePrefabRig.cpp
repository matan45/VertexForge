#include "PreviewServiceImpl.hpp"
#include "../../providers/render/IPrefabRigPreviewProvider.hpp"
#include "../../events/EventDispatcher.hpp"

// VK-1433 — Prefab Rig Preview service methods + CQRS handler registration.
// Split out of PreviewServiceImpl.cpp the way PreviewServiceAnimVFX.cpp is; every method
// delegates to prefabRigProvider (which may be null in headless/test contexts, so each is
// null-guarded and registration is skipped entirely when the provider is absent).

namespace services
{
    void PreviewServiceImpl::registerPrefabRigPreviewHandlers(::events::EventDispatcher& dispatcher)
    {
        if (!prefabRigProvider)
        {
            return;
        }

        using namespace events::prefabrigpreview;

        dispatcher.registerCommandHandler<InitPrefabRigPreviewCommand>(
            [this](const InitPrefabRigPreviewCommand& cmd)
            {
                initPrefabRigPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<BuildPrefabRigPreviewCommand>(
            [this](const BuildPrefabRigPreviewCommand& cmd) -> bool
            {
                return buildPrefabRigPreview(cmd.instanceId, cmd.desc);
            });

        dispatcher.registerCommandHandler<CleanUpPrefabRigPreviewCommand>(
            [this](const CleanUpPrefabRigPreviewCommand& cmd)
            {
                cleanUpPrefabRigPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<UpdatePrefabRigPreviewCommand>(
            [this](const UpdatePrefabRigPreviewCommand& cmd)
            {
                updatePrefabRigPreview(cmd.instanceId, cmd.deltaTime);
            });

        dispatcher.registerCommandHandler<UpdatePrefabRigCameraCommand>(
            [this](const UpdatePrefabRigCameraCommand& cmd)
            {
                updatePrefabRigCamera(cmd.instanceId, cmd.view, cmd.projection, cmd.cameraPos);
            });

        dispatcher.registerCommandHandler<SetPrefabRigEnvironmentCommand>(
            [this](const SetPrefabRigEnvironmentCommand& cmd)
            {
                setPrefabRigEnvironment(cmd.instanceId, cmd.params);
            });

        dispatcher.registerCommandHandler<SetPrefabRigRootMatrixCommand>(
            [this](const SetPrefabRigRootMatrixCommand& cmd)
            {
                setPrefabRigRootMatrix(cmd.instanceId, cmd.model);
            });

        dispatcher.registerCommandHandler<SetPrefabRigStateCommand>(
            [this](const SetPrefabRigStateCommand& cmd)
            {
                setPrefabRigState(cmd.instanceId, cmd.part, cmd.stateName, cmd.blendDuration);
            });

        dispatcher.registerCommandHandler<SetPrefabRigBoolCommand>(
            [this](const SetPrefabRigBoolCommand& cmd)
            {
                setPrefabRigBool(cmd.instanceId, cmd.part, cmd.name, cmd.value);
            });

        dispatcher.registerCommandHandler<SetPrefabRigFloatCommand>(
            [this](const SetPrefabRigFloatCommand& cmd)
            {
                setPrefabRigFloat(cmd.instanceId, cmd.part, cmd.name, cmd.value);
            });

        dispatcher.registerCommandHandler<SetPrefabRigIntCommand>(
            [this](const SetPrefabRigIntCommand& cmd)
            {
                setPrefabRigInt(cmd.instanceId, cmd.part, cmd.name, cmd.value);
            });

        dispatcher.registerCommandHandler<SetPrefabRigTriggerCommand>(
            [this](const SetPrefabRigTriggerCommand& cmd)
            {
                setPrefabRigTrigger(cmd.instanceId, cmd.part, cmd.name);
            });

        dispatcher.registerCommandHandler<PlayPrefabRigCommand>(
            [this](const PlayPrefabRigCommand& cmd)
            {
                playPrefabRig(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<PausePrefabRigCommand>(
            [this](const PausePrefabRigCommand& cmd)
            {
                pausePrefabRig(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<SetPrefabRigSocketsCommand>(
            [this](const SetPrefabRigSocketsCommand& cmd)
            {
                setPrefabRigSockets(cmd.instanceId, cmd.part, cmd.sockets);
            });

        dispatcher.registerCommandHandler<SetPrefabRigChainsCommand>(
            [this](const SetPrefabRigChainsCommand& cmd)
            {
                setPrefabRigChains(cmd.instanceId, cmd.chains);
            });

        dispatcher.registerCommandHandler<StepPrefabRigFrameCommand>(
            [this](const StepPrefabRigFrameCommand& cmd)
            {
                stepPrefabRigFrame(cmd.instanceId, cmd.part, cmd.frames);
            });

        dispatcher.registerCommandHandler<SetPrefabRigNormalizedTimeCommand>(
            [this](const SetPrefabRigNormalizedTimeCommand& cmd)
            {
                setPrefabRigNormalizedTime(cmd.instanceId, cmd.part, cmd.t);
            });

        dispatcher.registerCommandHandler<SetPrefabRigPartPreviewTransformCommand>(
            [this](const SetPrefabRigPartPreviewTransformCommand& cmd)
            {
                setPrefabRigPartPreviewTransform(cmd.instanceId, cmd.part, cmd.transform);
            });

        dispatcher.registerCommandHandler<ResetPrefabRigPreviewTransformsCommand>(
            [this](const ResetPrefabRigPreviewTransformsCommand& cmd)
            {
                resetPrefabRigPreviewTransforms(cmd.instanceId);
            });

        dispatcher.registerQueryHandler<RenderPrefabRigPreviewQuery>(
            [this](const RenderPrefabRigPreviewQuery& query)
            {
                return renderPrefabRigPreview(query.instanceId);
            });

        dispatcher.registerQueryHandler<IsPrefabRigPreviewBuiltQuery>(
            [this](const IsPrefabRigPreviewBuiltQuery& query)
            {
                return isPrefabRigPreviewBuilt(query.instanceId);
            });

        dispatcher.registerQueryHandler<GetPrefabRigPartCountQuery>(
            [this](const GetPrefabRigPartCountQuery& query)
            {
                return getPrefabRigPartCount(query.instanceId);
            });

        dispatcher.registerQueryHandler<IsPrefabRigPausedQuery>(
            [this](const IsPrefabRigPausedQuery& query)
            {
                return isPrefabRigPaused(query.instanceId);
            });

        dispatcher.registerQueryHandler<GetPrefabRigStatesQuery>(
            [this](const GetPrefabRigStatesQuery& query)
            {
                return getPrefabRigStates(query.instanceId, query.part);
            });

        dispatcher.registerQueryHandler<GetPrefabRigSocketsQuery>(
            [this](const GetPrefabRigSocketsQuery& query)
            {
                return getPrefabRigSockets(query.instanceId, query.part);
            });

        dispatcher.registerQueryHandler<GetPrefabRigChainsQuery>(
            [this](const GetPrefabRigChainsQuery& query)
            {
                return getPrefabRigChains(query.instanceId);
            });

        dispatcher.registerQueryHandler<GetPrefabRigNormalizedTimeQuery>(
            [this](const GetPrefabRigNormalizedTimeQuery& query)
            {
                return getPrefabRigNormalizedTime(query.instanceId, query.part);
            });

        dispatcher.registerQueryHandler<GetPrefabRigPartWorldQuery>(
            [this](const GetPrefabRigPartWorldQuery& query)
            {
                return getPrefabRigPartWorld(query.instanceId, query.part);
            });

        dispatcher.registerQueryHandler<GetPrefabRigJointWorldsQuery>(
            [this](const GetPrefabRigJointWorldsQuery& query)
            {
                return getPrefabRigJointWorlds(query.instanceId, query.part);
            });
    }

    // ---- Delegating method bodies ----

    void PreviewServiceImpl::initPrefabRigPreview(PreviewInstanceId instanceId)
    {
        if (prefabRigProvider)
            prefabRigProvider->initPrefabRigPreview(instanceId);
    }

    bool PreviewServiceImpl::buildPrefabRigPreview(PreviewInstanceId instanceId, const PrefabRigDescDTO& desc)
    {
        return prefabRigProvider ? prefabRigProvider->buildPrefabRigPreview(instanceId, desc) : false;
    }

    void PreviewServiceImpl::cleanUpPrefabRigPreview(PreviewInstanceId instanceId)
    {
        if (prefabRigProvider)
            prefabRigProvider->cleanUpPrefabRigPreview(instanceId);
    }

    bool PreviewServiceImpl::isPrefabRigPreviewBuilt(PreviewInstanceId instanceId) const
    {
        return prefabRigProvider ? prefabRigProvider->isPrefabRigPreviewBuilt(instanceId) : false;
    }

    size_t PreviewServiceImpl::getPrefabRigPartCount(PreviewInstanceId instanceId) const
    {
        return prefabRigProvider ? prefabRigProvider->getPrefabRigPartCount(instanceId) : 0;
    }

    void PreviewServiceImpl::updatePrefabRigPreview(PreviewInstanceId instanceId, float deltaTime)
    {
        if (prefabRigProvider)
            prefabRigProvider->updatePrefabRigPreview(instanceId, deltaTime);
    }

    void PreviewServiceImpl::updatePrefabRigCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                                   const glm::mat4& projection, const glm::vec3& cameraPos)
    {
        if (prefabRigProvider)
            prefabRigProvider->updatePrefabRigCamera(instanceId, view, projection, cameraPos);
    }

    void PreviewServiceImpl::setPrefabRigEnvironment(PreviewInstanceId instanceId,
                                                     const PreviewEnvironmentParams& params)
    {
        if (prefabRigProvider)
            prefabRigProvider->setPrefabRigEnvironment(instanceId, params);
    }

    void PreviewServiceImpl::setPrefabRigRootMatrix(PreviewInstanceId instanceId, const glm::mat4& model)
    {
        if (prefabRigProvider)
            prefabRigProvider->setPrefabRigRootMatrix(instanceId, model);
    }

    ViewportTextureHandle PreviewServiceImpl::renderPrefabRigPreview(PreviewInstanceId instanceId)
    {
        ViewportTextureHandle handle;
        if (prefabRigProvider)
            handle.imguiDescriptorSet = prefabRigProvider->renderPrefabRigPreview(instanceId);
        return handle;
    }

    void PreviewServiceImpl::setPrefabRigState(PreviewInstanceId instanceId, size_t part,
                                               const std::string& stateName, float blendDuration)
    {
        if (prefabRigProvider)
            prefabRigProvider->setPrefabRigState(instanceId, part, stateName, blendDuration);
    }

    std::vector<PrefabRigStateInfo> PreviewServiceImpl::getPrefabRigStates(PreviewInstanceId instanceId,
                                                                           size_t part) const
    {
        return prefabRigProvider ? prefabRigProvider->getPrefabRigStates(instanceId, part)
                                 : std::vector<PrefabRigStateInfo>{};
    }

    void PreviewServiceImpl::setPrefabRigBool(PreviewInstanceId instanceId, size_t part,
                                              const std::string& name, bool value)
    {
        if (prefabRigProvider)
            prefabRigProvider->setPrefabRigBool(instanceId, part, name, value);
    }

    void PreviewServiceImpl::setPrefabRigFloat(PreviewInstanceId instanceId, size_t part,
                                               const std::string& name, float value)
    {
        if (prefabRigProvider)
            prefabRigProvider->setPrefabRigFloat(instanceId, part, name, value);
    }

    void PreviewServiceImpl::setPrefabRigInt(PreviewInstanceId instanceId, size_t part,
                                             const std::string& name, int32_t value)
    {
        if (prefabRigProvider)
            prefabRigProvider->setPrefabRigInt(instanceId, part, name, value);
    }

    void PreviewServiceImpl::setPrefabRigTrigger(PreviewInstanceId instanceId, size_t part,
                                                 const std::string& name)
    {
        if (prefabRigProvider)
            prefabRigProvider->setPrefabRigTrigger(instanceId, part, name);
    }

    void PreviewServiceImpl::playPrefabRig(PreviewInstanceId instanceId)
    {
        if (prefabRigProvider)
            prefabRigProvider->playPrefabRig(instanceId);
    }

    void PreviewServiceImpl::pausePrefabRig(PreviewInstanceId instanceId)
    {
        if (prefabRigProvider)
            prefabRigProvider->pausePrefabRig(instanceId);
    }

    bool PreviewServiceImpl::isPrefabRigPaused(PreviewInstanceId instanceId) const
    {
        return prefabRigProvider ? prefabRigProvider->isPrefabRigPaused(instanceId) : false;
    }

    void PreviewServiceImpl::stepPrefabRigFrame(PreviewInstanceId instanceId, size_t part, int frames)
    {
        if (prefabRigProvider)
            prefabRigProvider->stepPrefabRigFrame(instanceId, part, frames);
    }

    void PreviewServiceImpl::setPrefabRigNormalizedTime(PreviewInstanceId instanceId, size_t part, float t)
    {
        if (prefabRigProvider)
            prefabRigProvider->setPrefabRigNormalizedTime(instanceId, part, t);
    }

    float PreviewServiceImpl::getPrefabRigNormalizedTime(PreviewInstanceId instanceId, size_t part) const
    {
        return prefabRigProvider ? prefabRigProvider->getPrefabRigNormalizedTime(instanceId, part) : 0.0f;
    }

    void PreviewServiceImpl::setPrefabRigPartPreviewTransform(PreviewInstanceId instanceId, size_t part,
                                                              const glm::mat4& transform)
    {
        if (prefabRigProvider)
            prefabRigProvider->setPrefabRigPartPreviewTransform(instanceId, part, transform);
    }

    void PreviewServiceImpl::resetPrefabRigPreviewTransforms(PreviewInstanceId instanceId)
    {
        if (prefabRigProvider)
            prefabRigProvider->resetPrefabRigPreviewTransforms(instanceId);
    }

    glm::mat4 PreviewServiceImpl::getPrefabRigPartWorld(PreviewInstanceId instanceId, size_t part) const
    {
        return prefabRigProvider ? prefabRigProvider->getPrefabRigPartWorld(instanceId, part)
                                 : glm::mat4(1.0f);
    }

    std::vector<PrefabRigJoint> PreviewServiceImpl::getPrefabRigJointWorlds(PreviewInstanceId instanceId,
                                                                            size_t part) const
    {
        return prefabRigProvider ? prefabRigProvider->getPrefabRigJointWorlds(instanceId, part)
                                 : std::vector<PrefabRigJoint>{};
    }

    std::vector<animator::SocketDefinition> PreviewServiceImpl::getPrefabRigSockets(PreviewInstanceId instanceId,
                                                                                    size_t part) const
    {
        return prefabRigProvider ? prefabRigProvider->getPrefabRigSockets(instanceId, part)
                                 : std::vector<animator::SocketDefinition>{};
    }

    void PreviewServiceImpl::setPrefabRigSockets(PreviewInstanceId instanceId, size_t part,
                                                 const std::vector<animator::SocketDefinition>& sockets)
    {
        if (prefabRigProvider)
            prefabRigProvider->setPrefabRigSockets(instanceId, part, sockets);
    }

    std::vector<animator::ik::IKChainConfig> PreviewServiceImpl::getPrefabRigChains(
        PreviewInstanceId instanceId) const
    {
        return prefabRigProvider ? prefabRigProvider->getPrefabRigChains(instanceId)
                                 : std::vector<animator::ik::IKChainConfig>{};
    }

    void PreviewServiceImpl::setPrefabRigChains(PreviewInstanceId instanceId,
                                                const std::vector<animator::ik::IKChainConfig>& chains)
    {
        if (prefabRigProvider)
            prefabRigProvider->setPrefabRigChains(instanceId, chains);
    }
}
