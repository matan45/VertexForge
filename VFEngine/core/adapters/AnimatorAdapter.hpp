#pragma once

#include "providers/IAnimatorProvider.hpp"
#include "AnimatorPreviewController.hpp"
#include <memory>
#include <unordered_map>

namespace core
{
    class AnimatorAdapter : public services::IAnimatorProvider
    {
    private:
        std::unordered_map<services::PreviewInstanceId, std::unique_ptr<::controllers::AnimatorPreviewController>>
        controllers;

    public:
        explicit AnimatorAdapter() = default;
        ~AnimatorAdapter() noexcept override;

        // Preview Instance Management
        void initAnimatorPreview(services::PreviewInstanceId instanceId) override;
        void cleanUpAnimatorPreview(services::PreviewInstanceId instanceId) override;

        // Animator Data Management
        bool loadAnimatorData(services::PreviewInstanceId instanceId, const std::string& path) override;
        bool saveAnimatorData(services::PreviewInstanceId instanceId, const std::string& path) override;
        bool createNewAnimator(services::PreviewInstanceId instanceId, const std::string& name) override;
        const animator::AnimatorData* getAnimatorData(services::PreviewInstanceId instanceId) const override;

        // State Management
        uint32_t addState(services::PreviewInstanceId instanceId, const std::string& name,
                          const glm::vec2& position) override;
        bool removeState(services::PreviewInstanceId instanceId, uint32_t stateId) override;
        bool updateState(services::PreviewInstanceId instanceId, uint32_t stateId,
                         const std::string& name, const std::string& animationPath,
                         float playbackSpeed, bool loop) override;
        bool setStatePosition(services::PreviewInstanceId instanceId, uint32_t stateId,
                              const glm::vec2& position) override;
        bool setDefaultState(services::PreviewInstanceId instanceId, uint32_t stateId) override;

        // Transition Management
        uint32_t addTransition(services::PreviewInstanceId instanceId, uint32_t sourceStateId,
                               uint32_t targetStateId) override;
        bool removeTransition(services::PreviewInstanceId instanceId, uint32_t transitionId) override;
        bool updateTransition(services::PreviewInstanceId instanceId, uint32_t transitionId,
                              float blendDuration, bool hasExitTime, float exitTime, int32_t priority) override;

        // Transition Condition Management
        bool addTransitionCondition(services::PreviewInstanceId instanceId, uint32_t transitionId,
                                    const std::string& parameterName,
                                    animator::ComparisonOperator op,
                                    const animator::AnimatorParameterValue& value) override;
        bool removeTransitionCondition(services::PreviewInstanceId instanceId, uint32_t transitionId,
                                       size_t conditionIndex) override;
        bool updateTransitionCondition(services::PreviewInstanceId instanceId, uint32_t transitionId,
                                       size_t conditionIndex, const std::string& parameterName,
                                       animator::ComparisonOperator op,
                                       const animator::AnimatorParameterValue& value) override;

        // Parameter Management
        bool addParameter(services::PreviewInstanceId instanceId, const std::string& name,
                          animator::AnimatorParameterType type,
                          const animator::AnimatorParameterValue& defaultValue) override;
        bool removeParameter(services::PreviewInstanceId instanceId, const std::string& name) override;
        bool updateParameter(services::PreviewInstanceId instanceId, const std::string& oldName,
                             const std::string& newName, animator::AnimatorParameterType type,
                             const animator::AnimatorParameterValue& defaultValue) override;

        // Node Graph Positions
        bool setAnyStatePosition(services::PreviewInstanceId instanceId, const glm::vec2& position) override;
        bool setEntryPosition(services::PreviewInstanceId instanceId, const glm::vec2& position) override;

        // Preview Playback
        void updateAnimatorPreview(services::PreviewInstanceId instanceId, float deltaTime) override;
        void playAnimatorPreview(services::PreviewInstanceId instanceId) override;
        void pauseAnimatorPreview(services::PreviewInstanceId instanceId) override;
        void stopAnimatorPreview(services::PreviewInstanceId instanceId) override;
        void resetAnimatorPreview(services::PreviewInstanceId instanceId) override;
        bool isAnimatorPlaying(services::PreviewInstanceId instanceId) const override;

        // Preview Parameters
        void setAnimatorPreviewFloat(services::PreviewInstanceId instanceId, const std::string& name,
                                     float value) override;
        void setAnimatorPreviewInt(services::PreviewInstanceId instanceId, const std::string& name,
                                   int32_t value) override;
        void setAnimatorPreviewBool(services::PreviewInstanceId instanceId, const std::string& name,
                                    bool value) override;
        void setAnimatorPreviewTrigger(services::PreviewInstanceId instanceId, const std::string& name) override;

        // State Info
        uint32_t getCurrentStateId(services::PreviewInstanceId instanceId) const override;
        float getCurrentStateTime(services::PreviewInstanceId instanceId) const override;
        float getNormalizedStateTime(services::PreviewInstanceId instanceId) const override;
        bool isBlending(services::PreviewInstanceId instanceId) const override;
        float getBlendWeight(services::PreviewInstanceId instanceId) const override;

        // Force Transition
        void forceTransitionTo(services::PreviewInstanceId instanceId, uint32_t stateId,
                               float blendDuration) override;

        // Rendering
        void setAnimatorPreviewRenderParams(services::PreviewInstanceId instanceId,
                                            const services::AnimatorPreviewRenderParams& params) override;
        void updateAnimatorCamera(services::PreviewInstanceId instanceId,
                                  const services::AnimatorPreviewCameraParams& camera) override;
        void* renderAnimatorPreview(services::PreviewInstanceId instanceId) override;

        // Bone Info
        std::vector<services::EvaluatedBoneInfo> getAnimatorPreviewBones(
            services::PreviewInstanceId instanceId) const override;

    private:
        controllers::AnimatorPreviewController* getController(services::PreviewInstanceId instanceId) const;
    };
}
