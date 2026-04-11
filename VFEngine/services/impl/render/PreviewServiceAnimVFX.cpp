#include "PreviewServiceImpl.hpp"
#include "../../providers/animation/IAnimationPreviewProvider.hpp"
#include "../../providers/vfx/IVFXPreviewProvider.hpp"

namespace services
{
    void PreviewServiceImpl::initAnimationPreview(PreviewInstanceId instanceId)
    {
        if (animationProvider)
            animationProvider->initAnimationPreview(instanceId);
    }

    void PreviewServiceImpl::cleanUpAnimationPreview(PreviewInstanceId instanceId)
    {
        if (animationProvider)
            animationProvider->cleanUpAnimationPreview(instanceId);
    }

    bool PreviewServiceImpl::loadAnimationPreviewMesh(PreviewInstanceId instanceId, const std::string& meshPath)
    {
        return animationProvider ? animationProvider->loadAnimationPreviewMesh(instanceId, meshPath) : false;
    }

    bool PreviewServiceImpl::loadAnimationPreviewAnimation(PreviewInstanceId instanceId, const std::string& animPath)
    {
        return animationProvider ? animationProvider->loadAnimationPreviewAnimation(instanceId, animPath) : false;
    }

    void PreviewServiceImpl::playAnimation(PreviewInstanceId instanceId)
    {
        if (animationProvider)
            animationProvider->playAnimation(instanceId);
    }

    void PreviewServiceImpl::pauseAnimation(PreviewInstanceId instanceId)
    {
        if (animationProvider)
            animationProvider->pauseAnimation(instanceId);
    }

    void PreviewServiceImpl::stopAnimation(PreviewInstanceId instanceId)
    {
        if (animationProvider)
            animationProvider->stopAnimation(instanceId);
    }

    bool PreviewServiceImpl::isAnimationPlaying(PreviewInstanceId instanceId) const
    {
        return animationProvider ? animationProvider->isAnimationPlaying(instanceId) : false;
    }

    void PreviewServiceImpl::setAnimationPlaybackTime(PreviewInstanceId instanceId, float timeSeconds)
    {
        if (animationProvider)
            animationProvider->setAnimationPlaybackTime(instanceId, timeSeconds);
    }

    float PreviewServiceImpl::getAnimationPlaybackTime(PreviewInstanceId instanceId) const
    {
        return animationProvider ? animationProvider->getAnimationPlaybackTime(instanceId) : 0.0f;
    }

    void PreviewServiceImpl::setAnimationLooping(PreviewInstanceId instanceId, bool loop)
    {
        if (animationProvider)
            animationProvider->setAnimationLooping(instanceId, loop);
    }

    void PreviewServiceImpl::setAnimationPlaybackSpeed(PreviewInstanceId instanceId, float speed)
    {
        if (animationProvider)
            animationProvider->setAnimationPlaybackSpeed(instanceId, speed);
    }

    void PreviewServiceImpl::updateAnimationPreview(PreviewInstanceId instanceId, float deltaTime)
    {
        if (animationProvider)
            animationProvider->updateAnimationPreview(instanceId, deltaTime);
    }

    void PreviewServiceImpl::setAnimationPreviewParams(PreviewInstanceId instanceId,
                                                       const AnimationPreviewParams& params)
    {
        if (animationProvider)
            animationProvider->setAnimationPreviewParams(instanceId, params);
    }

    void PreviewServiceImpl::setAnimationPreviewEnvironment(PreviewInstanceId instanceId,
                                                            const PreviewEnvironmentParams& params)
    {
        if (animationProvider)
            animationProvider->setAnimationPreviewEnvironment(instanceId, params);
    }

    void PreviewServiceImpl::updateAnimationCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                                   const glm::mat4& projection, const glm::vec3& cameraPos)
    {
        if (animationProvider)
            animationProvider->updateAnimationCamera(instanceId, view, projection, cameraPos);
    }

    ViewportTextureHandle PreviewServiceImpl::renderAnimationPreview(PreviewInstanceId instanceId)
    {
        ViewportTextureHandle handle;
        if (animationProvider)
            handle.imguiDescriptorSet = animationProvider->renderAnimationPreview(instanceId);
        return handle;
    }

    std::vector<EvaluatedBoneInfo> PreviewServiceImpl::getAnimationPreviewEvaluatedBones(
        PreviewInstanceId instanceId) const
    {
        return animationProvider ? animationProvider->getAnimationPreviewEvaluatedBones(instanceId)
                                : std::vector<EvaluatedBoneInfo>{};
    }

    void PreviewServiceImpl::initVFXPreview(PreviewInstanceId instanceId)
    {
        if (vfxProvider)
            vfxProvider->initVFXPreview(instanceId);
    }

    void PreviewServiceImpl::cleanUpVFXPreview(PreviewInstanceId instanceId)
    {
        if (vfxProvider)
            vfxProvider->cleanUpVFXPreview(instanceId);
    }

    void PreviewServiceImpl::setVFXParams(PreviewInstanceId instanceId, const VFXPreviewParams& params)
    {
        if (vfxProvider)
            vfxProvider->setVFXParams(instanceId, params);
    }

    void PreviewServiceImpl::updateVFXCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                              const glm::mat4& projection, const glm::vec3& cameraPos, float time)
    {
        if (vfxProvider)
            vfxProvider->updateVFXCamera(instanceId, view, projection, cameraPos, time);
    }

    void PreviewServiceImpl::updateVFXSimulation(PreviewInstanceId instanceId, float deltaTime)
    {
        if (vfxProvider)
            vfxProvider->updateVFXSimulation(instanceId, deltaTime);
    }

    void PreviewServiceImpl::playVFX(PreviewInstanceId instanceId)
    {
        if (vfxProvider)
            vfxProvider->playVFX(instanceId);
    }

    void PreviewServiceImpl::pauseVFX(PreviewInstanceId instanceId)
    {
        if (vfxProvider)
            vfxProvider->pauseVFX(instanceId);
    }

    void PreviewServiceImpl::stopVFX(PreviewInstanceId instanceId)
    {
        if (vfxProvider)
            vfxProvider->stopVFX(instanceId);
    }

    ViewportTextureHandle PreviewServiceImpl::renderVFXPreview(PreviewInstanceId instanceId)
    {
        ViewportTextureHandle handle;
        if (vfxProvider)
            handle.imguiDescriptorSet = vfxProvider->renderVFXPreview(instanceId);
        return handle;
    }
}
