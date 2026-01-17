#include "PreviewServiceImpl.hpp"
#include "../providers/IMaterialPreviewProvider.hpp"
#include "../providers/IMeshPreviewProvider.hpp"
#include "../providers/IAnimationPreviewProvider.hpp"
#include "../events/EventDispatcher.hpp"
#include <cassert>

namespace services
{
    PreviewServiceImpl::PreviewServiceImpl(IMaterialPreviewProvider* materialProv, IMeshPreviewProvider* meshProv,
                                           IAnimationPreviewProvider* animProv)
        : materialProvider(materialProv), meshProvider(meshProv), animationProvider(animProv)
    {
        assert(materialProvider != nullptr && "PreviewServiceImpl requires a valid IMaterialPreviewProvider");
        assert(meshProvider != nullptr && "PreviewServiceImpl requires a valid IMeshPreviewProvider");
        // animationProvider can be null initially
    }

    PreviewServiceImpl::~PreviewServiceImpl() = default;

    void PreviewServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // Material Preview Commands
        dispatcher.registerCommandHandler<events::preview::InitMaterialPreviewCommand>(
            [this](const events::preview::InitMaterialPreviewCommand& cmd)
            {
                initMaterialPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::preview::CleanUpMaterialPreviewCommand>(
            [this](const events::preview::CleanUpMaterialPreviewCommand& cmd)
            {
                cleanUpMaterialPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::preview::SetMaterialParamsCommand>(
            [this](const events::preview::SetMaterialParamsCommand& cmd)
            {
                setMaterialParams(cmd.instanceId, cmd.params);
            });

        dispatcher.registerCommandHandler<events::preview::UpdateMaterialCameraCommand>(
            [this](const events::preview::UpdateMaterialCameraCommand& cmd)
            {
                updateMaterialCamera(cmd.instanceId, cmd.view, cmd.projection, cmd.cameraPos, cmd.time);
            });

        // Material Preview Queries
        dispatcher.registerQueryHandler<events::preview::RenderMaterialPreviewQuery>(
            [this](const events::preview::RenderMaterialPreviewQuery& query)
            {
                return renderMaterialPreview(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::preview::GetMaterialShaderErrorQuery>(
            [this](const events::preview::GetMaterialShaderErrorQuery& query)
            {
                return getMaterialShaderError(query.instanceId);
            });

        // Mesh Preview Commands
        dispatcher.registerCommandHandler<events::preview::InitMeshPreviewCommand>(
            [this](const events::preview::InitMeshPreviewCommand& cmd)
            {
                initMeshPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::preview::CleanUpMeshPreviewCommand>(
            [this](const events::preview::CleanUpMeshPreviewCommand& cmd)
            {
                cleanUpMeshPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::preview::SetMeshPreviewParamsCommand>(
            [this](const events::preview::SetMeshPreviewParamsCommand& cmd)
            {
                setMeshPreviewParams(cmd.instanceId, cmd.params);
            });

        dispatcher.registerCommandHandler<events::preview::UpdateMeshCameraCommand>(
            [this](const events::preview::UpdateMeshCameraCommand& cmd)
            {
                updateMeshCamera(cmd.instanceId, cmd.view, cmd.projection, cmd.cameraPos);
            });

        // Mesh Preview Queries
        dispatcher.registerQueryHandler<events::preview::GetPreviewMeshSubMeshInfoQuery>(
            [this](const events::preview::GetPreviewMeshSubMeshInfoQuery& query)
            {
                return getPreviewMeshSubMeshInfo(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::preview::GetPreviewMeshLODInfoQuery>(
            [this](const events::preview::GetPreviewMeshLODInfoQuery& query)
            {
                return getPreviewMeshLODInfo(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::preview::GetPreviewMeshBoundsQuery>(
            [this](const events::preview::GetPreviewMeshBoundsQuery& query)
            {
                return getPreviewMeshBounds(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::preview::RenderMeshPreviewQuery>(
            [this](const events::preview::RenderMeshPreviewQuery& query)
            {
                return renderMeshPreview(query.instanceId);
            });

        // Async Mesh Loading Commands
        dispatcher.registerCommandHandler<events::preview::LoadPreviewMeshAsyncCommand>(
            [this](const events::preview::LoadPreviewMeshAsyncCommand& cmd)
            {
                loadPreviewMeshAsync(cmd.instanceId, cmd.meshPath);
            });

        dispatcher.registerCommandHandler<events::preview::CancelMeshLoadingCommand>(
            [this](const events::preview::CancelMeshLoadingCommand& cmd)
            {
                cancelMeshLoading(cmd.instanceId);
            });

        // Async Mesh Loading Queries
        dispatcher.registerQueryHandler<events::preview::GetMeshLoadingProgressQuery>(
            [this](const events::preview::GetMeshLoadingProgressQuery& query)
            {
                return getMeshLoadingProgress(query.instanceId);
            });

        // Animation Preview Commands (only register if provider is available)
        if (animationProvider)
        {
            dispatcher.registerCommandHandler<events::animpreview::InitAnimationPreviewCommand>(
                [this](const events::animpreview::InitAnimationPreviewCommand& cmd)
                {
                    initAnimationPreview(cmd.instanceId);
                });

            dispatcher.registerCommandHandler<events::animpreview::CleanUpAnimationPreviewCommand>(
                [this](const events::animpreview::CleanUpAnimationPreviewCommand& cmd)
                {
                    cleanUpAnimationPreview(cmd.instanceId);
                });

            dispatcher.registerCommandHandler<events::animpreview::LoadAnimationPreviewMeshCommand>(
                [this](const events::animpreview::LoadAnimationPreviewMeshCommand& cmd) -> bool
                {
                    return loadAnimationPreviewMesh(cmd.instanceId, cmd.meshPath);
                });

            dispatcher.registerCommandHandler<events::animpreview::LoadAnimationPreviewAnimationCommand>(
                [this](const events::animpreview::LoadAnimationPreviewAnimationCommand& cmd) -> bool
                {
                    return loadAnimationPreviewAnimation(cmd.instanceId, cmd.animationPath);
                });

            dispatcher.registerCommandHandler<events::animpreview::PlayAnimationCommand>(
                [this](const events::animpreview::PlayAnimationCommand& cmd)
                {
                    playAnimation(cmd.instanceId);
                });

            dispatcher.registerCommandHandler<events::animpreview::PauseAnimationCommand>(
                [this](const events::animpreview::PauseAnimationCommand& cmd)
                {
                    pauseAnimation(cmd.instanceId);
                });

            dispatcher.registerCommandHandler<events::animpreview::StopAnimationCommand>(
                [this](const events::animpreview::StopAnimationCommand& cmd)
                {
                    stopAnimation(cmd.instanceId);
                });

            dispatcher.registerCommandHandler<events::animpreview::SetAnimationPlaybackTimeCommand>(
                [this](const events::animpreview::SetAnimationPlaybackTimeCommand& cmd)
                {
                    setAnimationPlaybackTime(cmd.instanceId, cmd.timeSeconds);
                });

            dispatcher.registerCommandHandler<events::animpreview::SetAnimationLoopingCommand>(
                [this](const events::animpreview::SetAnimationLoopingCommand& cmd)
                {
                    setAnimationLooping(cmd.instanceId, cmd.looping);
                });

            dispatcher.registerCommandHandler<events::animpreview::SetAnimationPlaybackSpeedCommand>(
                [this](const events::animpreview::SetAnimationPlaybackSpeedCommand& cmd)
                {
                    setAnimationPlaybackSpeed(cmd.instanceId, cmd.speed);
                });

            dispatcher.registerCommandHandler<events::animpreview::UpdateAnimationPreviewCommand>(
                [this](const events::animpreview::UpdateAnimationPreviewCommand& cmd)
                {
                    updateAnimationPreview(cmd.instanceId, cmd.deltaTime);
                });

            dispatcher.registerCommandHandler<events::animpreview::SetAnimationPreviewParamsCommand>(
                [this](const events::animpreview::SetAnimationPreviewParamsCommand& cmd)
                {
                    setAnimationPreviewParams(cmd.instanceId, cmd.params);
                });

            dispatcher.registerCommandHandler<events::animpreview::UpdateAnimationCameraCommand>(
                [this](const events::animpreview::UpdateAnimationCameraCommand& cmd)
                {
                    updateAnimationCamera(cmd.instanceId, cmd.view, cmd.projection, cmd.cameraPos);
                });

            dispatcher.registerQueryHandler<events::animpreview::IsAnimationPlayingQuery>(
                [this](const events::animpreview::IsAnimationPlayingQuery& query)
                {
                    return isAnimationPlaying(query.instanceId);
                });

            dispatcher.registerQueryHandler<events::animpreview::GetAnimationPlaybackTimeQuery>(
                [this](const events::animpreview::GetAnimationPlaybackTimeQuery& query)
                {
                    return getAnimationPlaybackTime(query.instanceId);
                });

            dispatcher.registerQueryHandler<events::animpreview::RenderAnimationPreviewQuery>(
                [this](const events::animpreview::RenderAnimationPreviewQuery& query)
                {
                    return renderAnimationPreview(query.instanceId);
                });

            dispatcher.registerQueryHandler<events::animpreview::GetAnimationPreviewEvaluatedBonesQuery>(
                [this](const events::animpreview::GetAnimationPreviewEvaluatedBonesQuery& query)
                {
                    return getAnimationPreviewEvaluatedBones(query.instanceId);
                });
        }
    }

    // === Material Preview ===

    void PreviewServiceImpl::initMaterialPreview(PreviewInstanceId instanceId)
    {
        materialProvider->initMaterialPreview(instanceId);
    }

    void PreviewServiceImpl::cleanUpMaterialPreview(PreviewInstanceId instanceId)
    {
        materialProvider->cleanUpMaterialPreview(instanceId);
    }

    void PreviewServiceImpl::setMaterialParams(PreviewInstanceId instanceId, const MaterialPreviewParams& params)
    {
        materialProvider->setMaterialParams(instanceId, params);
    }

    void PreviewServiceImpl::updateMaterialCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                                  const glm::mat4& projection,
                                                  const glm::vec3& cameraPos, float time)
    {
        materialProvider->updateMaterialCamera(instanceId, view, projection, cameraPos, time);
    }

    ViewportTextureHandle PreviewServiceImpl::renderMaterialPreview(PreviewInstanceId instanceId)
    {
        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = materialProvider->renderMaterialPreview(instanceId);
        return handle;
    }

    std::string PreviewServiceImpl::getMaterialShaderError(PreviewInstanceId instanceId) const
    {
        return materialProvider->getMaterialShaderError(instanceId);
    }

    // === Mesh Preview ===

    void PreviewServiceImpl::initMeshPreview(PreviewInstanceId instanceId)
    {
        meshProvider->initMeshPreview(instanceId);
    }

    void PreviewServiceImpl::cleanUpMeshPreview(PreviewInstanceId instanceId)
    {
        meshProvider->cleanUpMeshPreview(instanceId);
    }

    std::vector<SubMeshInfo> PreviewServiceImpl::getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const
    {
        return meshProvider->getPreviewMeshSubMeshInfo(instanceId);
    }

    std::vector<LODInfo> PreviewServiceImpl::getPreviewMeshLODInfo(PreviewInstanceId instanceId) const
    {
        return meshProvider->getPreviewMeshLODInfo(instanceId);
    }

    math::AABB PreviewServiceImpl::getPreviewMeshBounds(PreviewInstanceId instanceId) const
    {
        return meshProvider->getPreviewMeshBounds(instanceId);
    }

    void PreviewServiceImpl::setMeshPreviewParams(PreviewInstanceId instanceId, const MeshPreviewParams& params)
    {
        meshProvider->setMeshPreviewParams(instanceId, params);
    }

    void PreviewServiceImpl::updateMeshCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                              const glm::mat4& projection,
                                              const glm::vec3& cameraPos)
    {
        meshProvider->updateMeshCamera(instanceId, view, projection, cameraPos);
    }

    ViewportTextureHandle PreviewServiceImpl::renderMeshPreview(PreviewInstanceId instanceId)
    {
        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = meshProvider->renderMeshPreview(instanceId);
        return handle;
    }

    // === Async Mesh Loading ===

    void PreviewServiceImpl::loadPreviewMeshAsync(PreviewInstanceId instanceId, const std::string& meshPath)
    {
        meshProvider->loadPreviewMeshAsync(instanceId, meshPath);
    }

    void PreviewServiceImpl::cancelMeshLoading(PreviewInstanceId instanceId)
    {
        meshProvider->cancelMeshLoading(instanceId);
    }

    MeshLoadingProgress PreviewServiceImpl::getMeshLoadingProgress(PreviewInstanceId instanceId) const
    {
        return meshProvider->getMeshLoadingProgress(instanceId);
    }

    void PreviewServiceImpl::processAsyncLoading()
    {
        meshProvider->processAsyncLoading();
    }

    // === Animation Preview ===

    void PreviewServiceImpl::initAnimationPreview(PreviewInstanceId instanceId)
    {
        if (animationProvider)
        {
            animationProvider->initAnimationPreview(instanceId);
        }
    }

    void PreviewServiceImpl::cleanUpAnimationPreview(PreviewInstanceId instanceId)
    {
        if (animationProvider)
        {
            animationProvider->cleanUpAnimationPreview(instanceId);
        }
    }

    bool PreviewServiceImpl::loadAnimationPreviewMesh(PreviewInstanceId instanceId, const std::string& meshPath)
    {
        if (animationProvider)
        {
            return animationProvider->loadAnimationPreviewMesh(instanceId, meshPath);
        }
        return false;
    }

    bool PreviewServiceImpl::loadAnimationPreviewAnimation(PreviewInstanceId instanceId, const std::string& animPath)
    {
        if (animationProvider)
        {
            return animationProvider->loadAnimationPreviewAnimation(instanceId, animPath);
        }
        return false;
    }

    void PreviewServiceImpl::playAnimation(PreviewInstanceId instanceId)
    {
        if (animationProvider)
        {
            animationProvider->playAnimation(instanceId);
        }
    }

    void PreviewServiceImpl::pauseAnimation(PreviewInstanceId instanceId)
    {
        if (animationProvider)
        {
            animationProvider->pauseAnimation(instanceId);
        }
    }

    void PreviewServiceImpl::stopAnimation(PreviewInstanceId instanceId)
    {
        if (animationProvider)
        {
            animationProvider->stopAnimation(instanceId);
        }
    }

    bool PreviewServiceImpl::isAnimationPlaying(PreviewInstanceId instanceId) const
    {
        if (animationProvider)
        {
            return animationProvider->isAnimationPlaying(instanceId);
        }
        return false;
    }

    void PreviewServiceImpl::setAnimationPlaybackTime(PreviewInstanceId instanceId, float timeSeconds)
    {
        if (animationProvider)
        {
            animationProvider->setAnimationPlaybackTime(instanceId, timeSeconds);
        }
    }

    float PreviewServiceImpl::getAnimationPlaybackTime(PreviewInstanceId instanceId) const
    {
        if (animationProvider)
        {
            return animationProvider->getAnimationPlaybackTime(instanceId);
        }
        return 0.0f;
    }

    void PreviewServiceImpl::setAnimationLooping(PreviewInstanceId instanceId, bool loop)
    {
        if (animationProvider)
        {
            animationProvider->setAnimationLooping(instanceId, loop);
        }
    }

    void PreviewServiceImpl::setAnimationPlaybackSpeed(PreviewInstanceId instanceId, float speed)
    {
        if (animationProvider)
        {
            animationProvider->setAnimationPlaybackSpeed(instanceId, speed);
        }
    }

    void PreviewServiceImpl::updateAnimationPreview(PreviewInstanceId instanceId, float deltaTime)
    {
        if (animationProvider)
        {
            animationProvider->updateAnimationPreview(instanceId, deltaTime);
        }
    }

    void PreviewServiceImpl::setAnimationPreviewParams(PreviewInstanceId instanceId,
                                                       const AnimationPreviewParams& params)
    {
        if (animationProvider)
        {
            animationProvider->setAnimationPreviewParams(instanceId, params);
        }
    }

    void PreviewServiceImpl::updateAnimationCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                                   const glm::mat4& projection, const glm::vec3& cameraPos)
    {
        if (animationProvider)
        {
            animationProvider->updateAnimationCamera(instanceId, view, projection, cameraPos);
        }
    }

    ViewportTextureHandle PreviewServiceImpl::renderAnimationPreview(PreviewInstanceId instanceId)
    {
        ViewportTextureHandle handle;
        if (animationProvider)
        {
            handle.imguiDescriptorSet = animationProvider->renderAnimationPreview(instanceId);
        }
        return handle;
    }

    std::vector<EvaluatedBoneInfo> PreviewServiceImpl::getAnimationPreviewEvaluatedBones(
        PreviewInstanceId instanceId) const
    {
        if (animationProvider)
        {
            return animationProvider->getAnimationPreviewEvaluatedBones(instanceId);
        }
        return {};
    }
}
