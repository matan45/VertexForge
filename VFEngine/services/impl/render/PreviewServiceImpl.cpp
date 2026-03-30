#include "PreviewServiceImpl.hpp"
#include "../../providers/render/IMaterialPreviewProvider.hpp"
#include "../../providers/render/IMeshPreviewProvider.hpp"
#include "../../providers/animation/IAnimationPreviewProvider.hpp"
#include "../../providers/vfx/IVFXPreviewProvider.hpp"
#include "../../events/EventDispatcher.hpp"
#include <cassert>

namespace services
{
    PreviewServiceImpl::PreviewServiceImpl(IMaterialPreviewProvider* materialProv, IMeshPreviewProvider* meshProv,
                                           IAnimationPreviewProvider* animProv, IVFXPreviewProvider* vfxProv)
        : materialProvider(materialProv), meshProvider(meshProv), animationProvider(animProv), vfxProvider(vfxProv)
    {
        assert(materialProvider != nullptr && "PreviewServiceImpl requires a valid IMaterialPreviewProvider");
        assert(meshProvider != nullptr && "PreviewServiceImpl requires a valid IMeshPreviewProvider");
    }

    PreviewServiceImpl::~PreviewServiceImpl() = default;

    void PreviewServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        registerMaterialPreviewHandlers(dispatcher);
        registerMeshPreviewHandlers(dispatcher);
        registerAnimationPreviewHandlers(dispatcher);
        registerVFXPreviewHandlers(dispatcher);
    }

    void PreviewServiceImpl::registerMaterialPreviewHandlers(::events::EventDispatcher& dispatcher)
    {
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
    }

    void PreviewServiceImpl::registerMeshPreviewHandlers(::events::EventDispatcher& dispatcher)
    {
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

        dispatcher.registerCommandHandler<events::preview::SetPreviewEnvironmentCommand>(
            [this](const events::preview::SetPreviewEnvironmentCommand& cmd)
            {
                setPreviewEnvironment(cmd.instanceId, cmd.params);
            });

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

        dispatcher.registerQueryHandler<events::preview::GetMeshLoadingProgressQuery>(
            [this](const events::preview::GetMeshLoadingProgressQuery& query)
            {
                return getMeshLoadingProgress(query.instanceId);
            });
    }

    void PreviewServiceImpl::registerAnimationPreviewHandlers(::events::EventDispatcher& dispatcher)
    {
        if (!animationProvider)
        {
            return;
        }

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

    void PreviewServiceImpl::registerVFXPreviewHandlers(::events::EventDispatcher& dispatcher)
    {
        if (!vfxProvider)
        {
            return;
        }

        dispatcher.registerCommandHandler<events::vfxpreview::InitVFXPreviewCommand>(
            [this](const events::vfxpreview::InitVFXPreviewCommand& cmd)
            {
                initVFXPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::vfxpreview::CleanUpVFXPreviewCommand>(
            [this](const events::vfxpreview::CleanUpVFXPreviewCommand& cmd)
            {
                cleanUpVFXPreview(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::vfxpreview::SetVFXParamsCommand>(
            [this](const events::vfxpreview::SetVFXParamsCommand& cmd)
            {
                setVFXParams(cmd.instanceId, cmd.params);
            });

        dispatcher.registerCommandHandler<events::vfxpreview::UpdateVFXCameraCommand>(
            [this](const events::vfxpreview::UpdateVFXCameraCommand& cmd)
            {
                updateVFXCamera(cmd.instanceId, cmd.view, cmd.projection, cmd.cameraPos, cmd.time);
            });

        dispatcher.registerCommandHandler<events::vfxpreview::UpdateVFXSimulationCommand>(
            [this](const events::vfxpreview::UpdateVFXSimulationCommand& cmd)
            {
                updateVFXSimulation(cmd.instanceId, cmd.deltaTime);
            });

        dispatcher.registerCommandHandler<events::vfxpreview::PlayVFXCommand>(
            [this](const events::vfxpreview::PlayVFXCommand& cmd)
            {
                playVFX(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::vfxpreview::PauseVFXCommand>(
            [this](const events::vfxpreview::PauseVFXCommand& cmd)
            {
                pauseVFX(cmd.instanceId);
            });

        dispatcher.registerCommandHandler<events::vfxpreview::StopVFXCommand>(
            [this](const events::vfxpreview::StopVFXCommand& cmd)
            {
                stopVFX(cmd.instanceId);
            });

        dispatcher.registerQueryHandler<events::vfxpreview::RenderVFXPreviewQuery>(
            [this](const events::vfxpreview::RenderVFXPreviewQuery& query)
            {
                return renderVFXPreview(query.instanceId);
            });
    }

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

    void PreviewServiceImpl::setPreviewEnvironment(PreviewInstanceId instanceId, const PreviewEnvironmentParams& params)
    {
        meshProvider->setPreviewEnvironment(instanceId, params);
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

}
