#pragma once

#include "AnimatorStateMachine.hpp"
#include "AnimationEvaluator.hpp"
#include "AnimationBlender.hpp"
#include "animator/AnimationLayerTypes.hpp"
#include "animator/AnimatorTypes.hpp"
#include "resource/Types.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>

namespace animation
{
    struct AnimationLayerRuntime
    {
        std::string name;
        float weight = 1.0f;
        animator::LayerBlendMode blendMode = animator::LayerBlendMode::Override;
        animator::LayerSourceMode sourceMode = animator::LayerSourceMode::StateMachine;
        animator::BoneMask boneMask;
        bool hasMask = false;

        std::unique_ptr<AnimatorStateMachine> stateMachine;

        AnimationEvaluator directClipEvaluator;
        const resource::AnimationData* directClipData = nullptr;
        float clipTime = 0.0f;
        bool clipLoop = true;
        float clipSpeed = 1.0f;
        bool clipPlaying = true;

        animator::AdditiveReferencePose additiveRefPose = animator::AdditiveReferencePose::FirstFrame;
        float additiveRefFrame = 0.0f;
        std::vector<glm::mat4> referencePose;
    };

    class AnimationLayerStack
    {
    public:
        AnimationLayerStack();
        ~AnimationLayerStack();

        void initialize(const animator::AnimatorData& data, const resource::SkeletonData* skeleton,
                         AnimationLoadCallback loadCallback);

        void update(float deltaTime);

        const std::vector<glm::mat4>& getBoneMatrices() const { return finalBoneMatrices; }
        std::vector<glm::mat4>& getMutableBoneMatrices() { return finalBoneMatrices; }

        // Layer management
        void setLayerWeight(uint32_t layerIndex, float weight);
        float getLayerWeight(uint32_t layerIndex) const;
        uint32_t getLayerCount() const;
        std::string getLayerName(uint32_t layerIndex) const;

        // Base layer state machine access (backward compat)
        AnimatorStateMachine* getBaseStateMachine();
        const AnimatorStateMachine* getBaseStateMachine() const;

        // Per-layer state machine access
        AnimatorStateMachine* getLayerStateMachine(uint32_t layerIndex);
        const AnimatorStateMachine* getLayerStateMachine(uint32_t layerIndex) const;

        // Shared parameter access (applied to all layers)
        void setFloat(const std::string& name, float value);
        void setInt(const std::string& name, int32_t value);
        void setBool(const std::string& name, bool value);
        void setTrigger(const std::string& name);

        float getFloat(const std::string& name) const;
        int32_t getInt(const std::string& name) const;
        bool getBool(const std::string& name) const;

        // Playback control (all layers)
        void play();
        void pause();
        void stop();
        void reset();

        bool isInitialized() const { return initialized; }
        bool isPlaying() const;
        bool isBlending() const;

        std::string getCurrentStateName() const;
        float getNormalizedTime() const;

        bool forceTransitionTo(const std::string& stateName, float blendDuration = 0.25f);

        // Root motion (base layer only)
        void setRootMotionEnabled(bool enabled);
        glm::vec3 consumeRootMotionDelta();

        // Events (aggregated from all layers)
        const std::vector<const animator::AnimationEvent*>& getFiredEvents() const;

        // Socket computation
        void computeSocketTransforms(const std::vector<animator::SocketDefinition>& sockets,
                                      std::vector<glm::mat4>& outSocketModelTransforms) const;

        // Access to animator data for editor/service layer
        const animator::AnimatorData* getAnimatorData() const { return animatorData; }

    private:
        std::vector<AnimationLayerRuntime> layers;
        std::vector<glm::mat4> finalBoneMatrices;
        const animator::AnimatorData* animatorData = nullptr;
        const resource::SkeletonData* skeletonData = nullptr;
        AnimationLoadCallback animationLoadCallback;
        animator::AnimatorRuntimeParameters sharedParameters;
        bool initialized = false;
        mutable std::vector<const animator::AnimationEvent*> cachedFiredEvents;

        void blendLayers();
        void evaluateBaseLayerPose();
        std::vector<glm::mat4> evaluateLayerPose(AnimationLayerRuntime& layer);
        void applyOverlayLayer(AnimationLayerRuntime& layer);

        void resolveBoneMasks();
        const animator::BoneMaskDefinition* findBoneMaskDefinition(const std::string& maskName) const;
        void resolveBoneMaskIndices(AnimationLayerRuntime& layer, const animator::BoneMaskDefinition& maskDef);

        void initializeSharedParameters(const animator::AnimatorData& data);
        void initializeLayerRuntimes(const animator::AnimatorData& data);
        void initializeSingleGraphLayer(const animator::AnimatorData& data);

        void computeReferencePose(AnimationLayerRuntime& layer);
        const resource::AnimationData* findReferencePoseClip(const AnimationLayerRuntime& layer) const;
        void updateDirectClipPlayback(AnimationLayerRuntime& layer, float deltaTime);
        void collectFiredEvents();
    };
}
