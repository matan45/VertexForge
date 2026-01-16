#pragma once

#include "PreviewInstanceId.hpp"
#include "IAnimationPreviewProvider.hpp"  // For EvaluatedBoneInfo
#include "animator/AnimatorTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <optional>

namespace services
{
    // Animator preview camera settings
    struct AnimatorPreviewCameraParams
    {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::vec3 position{0.0f, 1.0f, 3.0f};
    };

    // Animator preview rendering params
    struct AnimatorPreviewRenderParams
    {
        glm::mat4 modelMatrix{1.0f};
        glm::vec4 albedo{0.8f, 0.8f, 0.8f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
    };

    class IAnimatorProvider
    {
    public:
        virtual ~IAnimatorProvider() = default;

        // ============================================================
        // Preview Instance Management
        // ============================================================

        virtual void initAnimatorPreview(PreviewInstanceId instanceId) = 0;
        virtual void cleanUpAnimatorPreview(PreviewInstanceId instanceId) = 0;

        // ============================================================
        // Animator Data Management
        // ============================================================

        virtual bool loadAnimatorData(PreviewInstanceId instanceId, const std::string& path) = 0;
        virtual bool saveAnimatorData(PreviewInstanceId instanceId, const std::string& path) = 0;
        virtual bool createNewAnimator(PreviewInstanceId instanceId, const std::string& name) = 0;
        virtual const animator::AnimatorData* getAnimatorData(PreviewInstanceId instanceId) const = 0;

        // ============================================================
        // State Management
        // ============================================================

        virtual uint32_t addState(PreviewInstanceId instanceId, const std::string& name, const glm::vec2& position) = 0;
        virtual bool removeState(PreviewInstanceId instanceId, uint32_t stateId) = 0;
        virtual bool updateState(PreviewInstanceId instanceId, uint32_t stateId,
                                 const std::string& name, const std::string& animationPath,
                                 float playbackSpeed, bool loop) = 0;
        virtual bool setStatePosition(PreviewInstanceId instanceId, uint32_t stateId, const glm::vec2& position) = 0;
        virtual bool setDefaultState(PreviewInstanceId instanceId, uint32_t stateId) = 0;

        // ============================================================
        // Transition Management
        // ============================================================

        virtual uint32_t addTransition(PreviewInstanceId instanceId, uint32_t sourceStateId, uint32_t targetStateId) = 0;
        virtual bool removeTransition(PreviewInstanceId instanceId, uint32_t transitionId) = 0;
        virtual bool updateTransition(PreviewInstanceId instanceId, uint32_t transitionId,
                                      float blendDuration, bool hasExitTime, float exitTime, int32_t priority) = 0;

        // ============================================================
        // Transition Condition Management
        // ============================================================

        virtual bool addTransitionCondition(PreviewInstanceId instanceId, uint32_t transitionId,
                                            const std::string& parameterName,
                                            animator::ComparisonOperator op,
                                            const animator::AnimatorParameterValue& value) = 0;
        virtual bool removeTransitionCondition(PreviewInstanceId instanceId, uint32_t transitionId, size_t conditionIndex) = 0;
        virtual bool updateTransitionCondition(PreviewInstanceId instanceId, uint32_t transitionId, size_t conditionIndex,
                                               const std::string& parameterName,
                                               animator::ComparisonOperator op,
                                               const animator::AnimatorParameterValue& value) = 0;

        // ============================================================
        // Parameter Management
        // ============================================================

        virtual bool addParameter(PreviewInstanceId instanceId, const std::string& name,
                                  animator::AnimatorParameterType type,
                                  const animator::AnimatorParameterValue& defaultValue) = 0;
        virtual bool removeParameter(PreviewInstanceId instanceId, const std::string& name) = 0;
        virtual bool updateParameter(PreviewInstanceId instanceId, const std::string& oldName,
                                     const std::string& newName, animator::AnimatorParameterType type,
                                     const animator::AnimatorParameterValue& defaultValue) = 0;

        // ============================================================
        // Node Graph Position Management
        // ============================================================

        virtual bool setAnyStatePosition(PreviewInstanceId instanceId, const glm::vec2& position) = 0;
        virtual bool setEntryPosition(PreviewInstanceId instanceId, const glm::vec2& position) = 0;

        // ============================================================
        // Preview Playback
        // ============================================================

        virtual void updateAnimatorPreview(PreviewInstanceId instanceId, float deltaTime) = 0;
        virtual void playAnimatorPreview(PreviewInstanceId instanceId) = 0;
        virtual void pauseAnimatorPreview(PreviewInstanceId instanceId) = 0;
        virtual void stopAnimatorPreview(PreviewInstanceId instanceId) = 0;
        virtual void resetAnimatorPreview(PreviewInstanceId instanceId) = 0;
        virtual bool isAnimatorPlaying(PreviewInstanceId instanceId) const = 0;

        // Set/get preview parameters
        virtual void setAnimatorPreviewFloat(PreviewInstanceId instanceId, const std::string& name, float value) = 0;
        virtual void setAnimatorPreviewInt(PreviewInstanceId instanceId, const std::string& name, int32_t value) = 0;
        virtual void setAnimatorPreviewBool(PreviewInstanceId instanceId, const std::string& name, bool value) = 0;
        virtual void setAnimatorPreviewTrigger(PreviewInstanceId instanceId, const std::string& name) = 0;

        // Get current state info
        virtual uint32_t getCurrentStateId(PreviewInstanceId instanceId) const = 0;
        virtual float getCurrentStateTime(PreviewInstanceId instanceId) const = 0;
        virtual float getNormalizedStateTime(PreviewInstanceId instanceId) const = 0;
        virtual bool isBlending(PreviewInstanceId instanceId) const = 0;
        virtual float getBlendWeight(PreviewInstanceId instanceId) const = 0;

        // Force transition for testing
        virtual void forceTransitionTo(PreviewInstanceId instanceId, uint32_t stateId, float blendDuration = 0.25f) = 0;

        // ============================================================
        // Preview Rendering
        // ============================================================

        virtual void setAnimatorPreviewRenderParams(PreviewInstanceId instanceId, const AnimatorPreviewRenderParams& params) = 0;
        virtual void updateAnimatorCamera(PreviewInstanceId instanceId, const AnimatorPreviewCameraParams& camera) = 0;
        virtual void* renderAnimatorPreview(PreviewInstanceId instanceId) = 0;

        // Get evaluated bones for visualization
        virtual std::vector<EvaluatedBoneInfo> getAnimatorPreviewBones(PreviewInstanceId instanceId) const = 0;
    };
}
