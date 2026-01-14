#include "AnimationPreviewAdapter.hpp"
#include "../../graphics/controllers/AnimatedMeshPreviewController.hpp"

namespace core
{
    AnimationPreviewAdapter::~AnimationPreviewAdapter() noexcept
    {
        controllers.clear();
    }

    controllers::AnimatedMeshPreviewController* AnimationPreviewAdapter::getController(
        services::PreviewInstanceId instanceId) const
    {
        auto it = controllers.find(instanceId);
        return (it != controllers.end()) ? it->second.get() : nullptr;
    }

    void AnimationPreviewAdapter::initAnimationPreview(services::PreviewInstanceId instanceId)
    {
        if (controllers.find(instanceId) == controllers.end())
        {
            auto controller = std::make_unique<controllers::AnimatedMeshPreviewController>();
            controller->init();
            controllers[instanceId] = std::move(controller);
        }
    }

    void AnimationPreviewAdapter::cleanUpAnimationPreview(services::PreviewInstanceId instanceId)
    {
        auto it = controllers.find(instanceId);
        if (it != controllers.end())
        {
            it->second->cleanUp();
            controllers.erase(it);
        }
    }

    bool AnimationPreviewAdapter::isAnimationPreviewInitialized(services::PreviewInstanceId instanceId) const
    {
        return controllers.find(instanceId) != controllers.end();
    }

    bool AnimationPreviewAdapter::loadAnimationPreviewMesh(services::PreviewInstanceId instanceId,
                                                           const std::string& meshPath)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->loadMesh(meshPath);
        }
        return false;
    }

    bool AnimationPreviewAdapter::loadAnimationPreviewAnimation(services::PreviewInstanceId instanceId,
                                                                 const std::string& animPath)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->loadAnimation(animPath);
        }
        return false;
    }

    void AnimationPreviewAdapter::unloadAnimationPreview(services::PreviewInstanceId instanceId)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->unload();
        }
    }

    bool AnimationPreviewAdapter::isAnimationPreviewMeshLoaded(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->isMeshLoaded();
        }
        return false;
    }

    bool AnimationPreviewAdapter::isAnimationPreviewAnimationLoaded(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->isAnimationLoaded();
        }
        return false;
    }

    math::AABB AnimationPreviewAdapter::getAnimationPreviewMeshBounds(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->getMeshBounds();
        }
        return math::AABB{};
    }

    void AnimationPreviewAdapter::playAnimation(services::PreviewInstanceId instanceId)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->play();
        }
    }

    void AnimationPreviewAdapter::pauseAnimation(services::PreviewInstanceId instanceId)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->pause();
        }
    }

    void AnimationPreviewAdapter::stopAnimation(services::PreviewInstanceId instanceId)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->stop();
        }
    }

    bool AnimationPreviewAdapter::isAnimationPlaying(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->isPlaying();
        }
        return false;
    }

    void AnimationPreviewAdapter::setAnimationPlaybackTime(services::PreviewInstanceId instanceId, float timeSeconds)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->setPlaybackTime(timeSeconds);
        }
    }

    float AnimationPreviewAdapter::getAnimationPlaybackTime(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->getPlaybackTime();
        }
        return 0.0f;
    }

    float AnimationPreviewAdapter::getAnimationDuration(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->getDuration();
        }
        return 0.0f;
    }

    void AnimationPreviewAdapter::setAnimationLooping(services::PreviewInstanceId instanceId, bool loop)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->setLooping(loop);
        }
    }

    bool AnimationPreviewAdapter::isAnimationLooping(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->isLooping();
        }
        return true;
    }

    void AnimationPreviewAdapter::setAnimationPlaybackSpeed(services::PreviewInstanceId instanceId, float speed)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->setPlaybackSpeed(speed);
        }
    }

    float AnimationPreviewAdapter::getAnimationPlaybackSpeed(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->getPlaybackSpeed();
        }
        return 1.0f;
    }

    void AnimationPreviewAdapter::updateAnimationPreview(services::PreviewInstanceId instanceId, float deltaTime)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->update(deltaTime);
        }
    }

    void AnimationPreviewAdapter::setAnimationPreviewParams(services::PreviewInstanceId instanceId,
                                                            const services::AnimationPreviewParams& params)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->setModelMatrix(params.modelMatrix);
            controller->setAlbedo(params.albedo);
            controller->setMetallic(params.metallic);
            controller->setRoughness(params.roughness);
        }
    }

    void AnimationPreviewAdapter::updateAnimationCamera(services::PreviewInstanceId instanceId,
                                                        const glm::mat4& view, const glm::mat4& projection,
                                                        const glm::vec3& cameraPos)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->updateCamera(view, projection, cameraPos);
        }
    }

    void* AnimationPreviewAdapter::renderAnimationPreview(services::PreviewInstanceId instanceId)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->render();
        }
        return nullptr;
    }

    size_t AnimationPreviewAdapter::getAnimationPreviewBoneCount(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->getBoneCount();
        }
        return 0;
    }

    std::vector<services::EvaluatedBoneInfo> AnimationPreviewAdapter::getAnimationPreviewEvaluatedBones(
        services::PreviewInstanceId instanceId) const
    {
        std::vector<services::EvaluatedBoneInfo> result;

        if (auto* controller = getController(instanceId))
        {
            const auto& bones = controller->getEvaluatedBones();
            result.reserve(bones.size());

            for (const auto& bone : bones)
            {
                services::EvaluatedBoneInfo info;
                info.position = bone.position;
                info.rotation = bone.rotation;
                info.scale = bone.scale;
                info.worldTransform = bone.worldTransform;
                result.push_back(info);
            }
        }

        return result;
    }
}
