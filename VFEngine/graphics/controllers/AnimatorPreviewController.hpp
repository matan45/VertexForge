#pragma once

#include "animator/AnimatorTypes.hpp"
#include "../animation/AnimatorStateMachine.hpp"
#include "../animation/AnimationEvaluator.hpp"
#include "resource/Types.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <functional>

namespace controllers
{
    class AnimatorPreviewController
    {
    public:
        AnimatorPreviewController();
        ~AnimatorPreviewController();

        void init();
        void cleanUp();

        // Animator data management
        bool loadAnimatorData(const std::string& path);
        bool saveAnimatorData(const std::string& path);
        bool createNewAnimator(const std::string& name);
        const animator::AnimatorData* getAnimatorData() const { return animatorData.get(); }
        animator::AnimatorData* getAnimatorDataMutable() { return animatorData.get(); }

        // State management
        uint32_t addState(const std::string& name, const glm::vec2& position);
        bool removeState(uint32_t stateId);
        bool updateState(uint32_t stateId, const std::string& name, const std::string& animationPath,
                         float playbackSpeed, bool loop);
        bool setStatePosition(uint32_t stateId, const glm::vec2& position);
        bool setDefaultState(uint32_t stateId);

        // Transition management
        uint32_t addTransition(uint32_t sourceStateId, uint32_t targetStateId);
        bool removeTransition(uint32_t transitionId);
        bool updateTransition(uint32_t transitionId, float blendDuration, bool hasExitTime,
                              float exitTime, int32_t priority);

        // Transition condition management
        bool addTransitionCondition(uint32_t transitionId, const std::string& parameterName,
                                    animator::ComparisonOperator op,
                                    const animator::AnimatorParameterValue& value);
        bool removeTransitionCondition(uint32_t transitionId, size_t conditionIndex);
        bool updateTransitionCondition(uint32_t transitionId, size_t conditionIndex,
                                       const std::string& parameterName,
                                       animator::ComparisonOperator op,
                                       const animator::AnimatorParameterValue& value);

        // Parameter management
        bool addParameter(const std::string& name, animator::AnimatorParameterType type,
                          const animator::AnimatorParameterValue& defaultValue);
        bool removeParameter(const std::string& name);
        bool updateParameter(const std::string& oldName, const std::string& newName,
                             animator::AnimatorParameterType type,
                             const animator::AnimatorParameterValue& defaultValue);

        // Node graph positions
        bool setAnyStatePosition(const glm::vec2& position);
        bool setEntryPosition(const glm::vec2& position);

        // Preview playback
        void update(float deltaTime);
        void play();
        void pause();
        void stop();
        void reset();
        bool isPlaying() const;

        // Preview parameters
        void setPreviewFloat(const std::string& name, float value);
        void setPreviewInt(const std::string& name, int32_t value);
        void setPreviewBool(const std::string& name, bool value);
        void setPreviewTrigger(const std::string& name);

        // State info
        uint32_t getCurrentStateId() const;
        float getCurrentStateTime() const;
        float getNormalizedStateTime() const;
        bool isBlending() const;
        float getBlendWeight() const;

        // Force transition
        void forceTransitionTo(uint32_t stateId, float blendDuration = 0.25f);

        // Rendering
        void setModelMatrix(const glm::mat4& matrix);
        void setAlbedo(const glm::vec4& albedo);
        void setMetallic(float metallic);
        void setRoughness(float roughness);
        void updateCamera(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos);
        void* render();

        // Bone info
        const std::vector<animation::EvaluatedBone>& getEvaluatedBones() const;
        const std::vector<resource::SkeletonBone>& getAnimationSkeleton() const;

        // Mark as modified (for editor)
        void markModified() { isModified = true; }
        bool isDataModified() const { return isModified; }
        void clearModified() { isModified = false; }

    private:
        const resource::AnimationData* loadAnimation(const std::string& path);
        void initializeStateMachine();

        std::unique_ptr<animator::AnimatorData> animatorData;
        std::unique_ptr<animation::AnimatorStateMachine> stateMachine;

        // Animation cache
        std::unordered_map<std::string, std::unique_ptr<resource::AnimationData>> animationCache;

        // Rendering state
        glm::mat4 modelMatrix{1.0f};
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projectionMatrix{1.0f};
        glm::vec3 cameraPosition{0.0f, 1.0f, 3.0f};
        glm::vec4 albedo{0.8f, 0.8f, 0.8f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;

        // For skeleton display when no animation is loaded
        std::vector<animation::EvaluatedBone> emptyBones;
        std::vector<resource::SkeletonBone> emptySkeleton;

        bool initialized = false;
        bool isModified = false;

        std::string currentAnimatorPath;
    };
}
