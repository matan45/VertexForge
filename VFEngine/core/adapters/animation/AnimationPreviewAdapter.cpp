#include "AnimationPreviewAdapter.hpp"
#include "../../graphics/controllers/preview/AnimatedMeshPreviewController.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/Log.hpp"

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

    bool AnimationPreviewAdapter::loadRetargetedAnimationPreview(
        services::PreviewInstanceId instanceId,
        const std::string& sourceAnimPath,
        const std::string& sourceMeshPath,
        const retargeting::HumanoidRigData& sourceRig,
        const retargeting::HumanoidRigData& targetRig,
        const retargeting::RetargetMapData& map)
    {
        auto* controller = getController(instanceId);
        if (!controller)
            return false;

        // Read the source skeleton from its mesh (the clips were authored against it).
        resource::SkeletonData sourceSkeleton;
        resource::MeshStreamHandle handle;
        if (!handle.openStream(sourceMeshPath) || !handle.hasSkeletonData() ||
            !handle.readSkeleton(sourceSkeleton))
        {
            vfLogError("Retarget preview: failed to read source skeleton from {}", sourceMeshPath);
            return false;
        }

        return controller->loadRetargetedAnimation(sourceAnimPath, sourceSkeleton, sourceRig, targetRig, map);
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

    void AnimationPreviewAdapter::setAnimationLooping(services::PreviewInstanceId instanceId, bool loop)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->setLooping(loop);
        }
    }

    void AnimationPreviewAdapter::setAnimationPlaybackSpeed(services::PreviewInstanceId instanceId, float speed)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->setPlaybackSpeed(speed);
        }
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
            controller->setClearColor(params.clearColor);
        }
    }

    void AnimationPreviewAdapter::setAnimationPreviewEnvironment(services::PreviewInstanceId instanceId,
                                                                 const services::PreviewEnvironmentParams& params)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->setEnvironment(params);
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

    std::vector<services::EvaluatedBoneInfo> AnimationPreviewAdapter::getAnimationPreviewEvaluatedBones(
        services::PreviewInstanceId instanceId) const
    {
        std::vector<services::EvaluatedBoneInfo> result;

        if (auto* controller = getController(instanceId))
        {
            const auto& bones = controller->getEvaluatedBones();
            const auto& skeleton = controller->getSkeleton();
            result.reserve(bones.size());

            for (size_t i = 0; i < bones.size() && i < skeleton.size(); ++i)
            {
                const auto& bone = bones[i];
                const auto& skelBone = skeleton[i];

                services::EvaluatedBoneInfo info;
                info.name = skelBone.name;
                info.position = bone.position;
                info.rotation = bone.rotation;
                info.scale = bone.scale;
                info.worldTransform = bone.worldTransform;
                info.skinnedPosition = bone.skinnedPosition;
                info.parentIndex = skelBone.parentIndex;
                result.push_back(info);
            }
        }

        return result;
    }
}
