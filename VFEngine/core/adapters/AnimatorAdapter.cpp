#include "AnimatorAdapter.hpp"
#include "AnimatorPreviewController.hpp"
#include "print/EditorLogger.hpp"

namespace core
{
    AnimatorAdapter::~AnimatorAdapter() noexcept
    {
        controllers.clear();
    }

    controllers::AnimatorPreviewController* AnimatorAdapter::getController(
        services::PreviewInstanceId instanceId) const
    {
        auto it = controllers.find(instanceId);
        return (it != controllers.end()) ? it->second.get() : nullptr;
    }

    void AnimatorAdapter::initAnimatorPreview(services::PreviewInstanceId instanceId)
    {
        if (controllers.find(instanceId) == controllers.end())
        {
            auto controller = std::make_unique<controllers::AnimatorPreviewController>();
            controller->init();
            controllers[instanceId] = std::move(controller);
        }
    }

    void AnimatorAdapter::cleanUpAnimatorPreview(services::PreviewInstanceId instanceId)
    {
        auto it = controllers.find(instanceId);
        if (it != controllers.end())
        {
            it->second->cleanUp();
            controllers.erase(it);
        }
    }

    bool AnimatorAdapter::loadAnimatorData(services::PreviewInstanceId instanceId, const std::string& path)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->loadAnimatorData(path);
        }
        return false;
    }

    bool AnimatorAdapter::saveAnimatorData(services::PreviewInstanceId instanceId, const std::string& path)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->saveAnimatorData(path);
        }
        return false;
    }

    bool AnimatorAdapter::createNewAnimator(services::PreviewInstanceId instanceId, const std::string& name)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->createNewAnimator(name);
        }
        return false;
    }

    const animator::AnimatorData* AnimatorAdapter::getAnimatorData(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->getAnimatorData();
        }
        return nullptr;
    }

    uint32_t AnimatorAdapter::addState(services::PreviewInstanceId instanceId, const std::string& name,
                                       const glm::vec2& position)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->addState(name, position);
        }
        return 0;
    }

    bool AnimatorAdapter::removeState(services::PreviewInstanceId instanceId, uint32_t stateId)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->removeState(stateId);
        }
        return false;
    }

    bool AnimatorAdapter::updateState(services::PreviewInstanceId instanceId, uint32_t stateId,
                                      const std::string& name, const std::string& animationPath,
                                      float playbackSpeed, bool loop)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->updateState(stateId, name, animationPath, playbackSpeed, loop);
        }
        return false;
    }

    bool AnimatorAdapter::setStatePosition(services::PreviewInstanceId instanceId, uint32_t stateId,
                                           const glm::vec2& position)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->setStatePosition(stateId, position);
        }
        return false;
    }

    bool AnimatorAdapter::setDefaultState(services::PreviewInstanceId instanceId, uint32_t stateId)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->setDefaultState(stateId);
        }
        return false;
    }

    uint32_t AnimatorAdapter::addTransition(services::PreviewInstanceId instanceId,
                                            uint32_t sourceStateId, uint32_t targetStateId)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->addTransition(sourceStateId, targetStateId);
        }
        return 0;
    }

    bool AnimatorAdapter::removeTransition(services::PreviewInstanceId instanceId, uint32_t transitionId)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->removeTransition(transitionId);
        }
        return false;
    }

    bool AnimatorAdapter::updateTransition(services::PreviewInstanceId instanceId, uint32_t transitionId,
                                           float blendDuration, bool hasExitTime, float exitTime, int32_t priority)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->updateTransition(transitionId, blendDuration, hasExitTime, exitTime, priority);
        }
        return false;
    }

    bool AnimatorAdapter::addTransitionCondition(services::PreviewInstanceId instanceId, uint32_t transitionId,
                                                 const std::string& parameterName,
                                                 animator::ComparisonOperator op,
                                                 const animator::AnimatorParameterValue& value)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->addTransitionCondition(transitionId, parameterName, op, value);
        }
        return false;
    }

    bool AnimatorAdapter::removeTransitionCondition(services::PreviewInstanceId instanceId, uint32_t transitionId,
                                                    size_t conditionIndex)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->removeTransitionCondition(transitionId, conditionIndex);
        }
        return false;
    }

    bool AnimatorAdapter::updateTransitionCondition(services::PreviewInstanceId instanceId, uint32_t transitionId,
                                                    size_t conditionIndex, const std::string& parameterName,
                                                    animator::ComparisonOperator op,
                                                    const animator::AnimatorParameterValue& value)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->updateTransitionCondition(transitionId, conditionIndex, parameterName, op, value);
        }
        return false;
    }

    bool AnimatorAdapter::addParameter(services::PreviewInstanceId instanceId, const std::string& name,
                                       animator::AnimatorParameterType type,
                                       const animator::AnimatorParameterValue& defaultValue)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->addParameter(name, type, defaultValue);
        }
        return false;
    }

    bool AnimatorAdapter::removeParameter(services::PreviewInstanceId instanceId, const std::string& name)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->removeParameter(name);
        }
        return false;
    }

    bool AnimatorAdapter::updateParameter(services::PreviewInstanceId instanceId, const std::string& oldName,
                                          const std::string& newName, animator::AnimatorParameterType type,
                                          const animator::AnimatorParameterValue& defaultValue)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->updateParameter(oldName, newName, type, defaultValue);
        }
        return false;
    }

    bool AnimatorAdapter::setAnyStatePosition(services::PreviewInstanceId instanceId, const glm::vec2& position)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->setAnyStatePosition(position);
        }
        return false;
    }

    bool AnimatorAdapter::setEntryPosition(services::PreviewInstanceId instanceId, const glm::vec2& position)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->setEntryPosition(position);
        }
        return false;
    }

    void AnimatorAdapter::updateAnimatorPreview(services::PreviewInstanceId instanceId, float deltaTime)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->update(deltaTime);
        }
    }

    void AnimatorAdapter::playAnimatorPreview(services::PreviewInstanceId instanceId)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->play();
        }
    }

    void AnimatorAdapter::pauseAnimatorPreview(services::PreviewInstanceId instanceId)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->pause();
        }
    }

    void AnimatorAdapter::stopAnimatorPreview(services::PreviewInstanceId instanceId)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->stop();
        }
    }

    void AnimatorAdapter::resetAnimatorPreview(services::PreviewInstanceId instanceId)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->reset();
        }
    }

    bool AnimatorAdapter::isAnimatorPlaying(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->isPlaying();
        }
        return false;
    }

    void AnimatorAdapter::setAnimatorPreviewFloat(services::PreviewInstanceId instanceId,
                                                  const std::string& name, float value)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->setPreviewFloat(name, value);
        }
    }

    void AnimatorAdapter::setAnimatorPreviewInt(services::PreviewInstanceId instanceId,
                                                const std::string& name, int32_t value)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->setPreviewInt(name, value);
        }
    }

    void AnimatorAdapter::setAnimatorPreviewBool(services::PreviewInstanceId instanceId,
                                                 const std::string& name, bool value)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->setPreviewBool(name, value);
        }
    }

    void AnimatorAdapter::setAnimatorPreviewTrigger(services::PreviewInstanceId instanceId, const std::string& name)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->setPreviewTrigger(name);
        }
    }

    uint32_t AnimatorAdapter::getCurrentStateId(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->getCurrentStateId();
        }
        return 0;
    }

    float AnimatorAdapter::getCurrentStateTime(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->getCurrentStateTime();
        }
        return 0.0f;
    }

    float AnimatorAdapter::getNormalizedStateTime(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->getNormalizedStateTime();
        }
        return 0.0f;
    }

    bool AnimatorAdapter::isBlending(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->isBlending();
        }
        return false;
    }

    float AnimatorAdapter::getBlendWeight(services::PreviewInstanceId instanceId) const
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->getBlendWeight();
        }
        return 0.0f;
    }

    void AnimatorAdapter::forceTransitionTo(services::PreviewInstanceId instanceId, uint32_t stateId,
                                            float blendDuration)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->forceTransitionTo(stateId, blendDuration);
        }
    }

    void AnimatorAdapter::setAnimatorPreviewRenderParams(services::PreviewInstanceId instanceId,
                                                         const services::AnimatorPreviewRenderParams& params)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->setModelMatrix(params.modelMatrix);
            controller->setAlbedo(params.albedo);
            controller->setMetallic(params.metallic);
            controller->setRoughness(params.roughness);
        }
    }

    void AnimatorAdapter::updateAnimatorCamera(services::PreviewInstanceId instanceId,
                                               const services::AnimatorPreviewCameraParams& camera)
    {
        if (auto* controller = getController(instanceId))
        {
            controller->updateCamera(camera.view, camera.projection, camera.position);
        }
    }

    void* AnimatorAdapter::renderAnimatorPreview(services::PreviewInstanceId instanceId)
    {
        if (auto* controller = getController(instanceId))
        {
            return controller->render();
        }
        return nullptr;
    }

    std::vector<services::EvaluatedBoneInfo> AnimatorAdapter::getAnimatorPreviewBones(
        services::PreviewInstanceId instanceId) const
    {
        std::vector<services::EvaluatedBoneInfo> result;

        if (auto* controller = getController(instanceId))
        {
            const auto& bones = controller->getEvaluatedBones();
            const auto& skeleton = controller->getAnimationSkeleton();

            // Bones and skeleton should always have matching sizes since bones are evaluated from the skeleton.
            // A mismatch indicates a bug in animation evaluation or skeleton loading.
            if (bones.size() != skeleton.size())
            {
                vfLogWarning("[AnimatorAdapter] Bone/skeleton size mismatch: {} evaluated bones vs {} skeleton bones. "
                             "Results may be incomplete.",
                             bones.size(), skeleton.size());
            }

            const size_t count = std::min(bones.size(), skeleton.size());
            result.reserve(count);

            for (size_t i = 0; i < count; ++i)
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
