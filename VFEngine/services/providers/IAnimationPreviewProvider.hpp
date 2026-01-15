#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <math/Frustum.hpp>
#include "PreviewInstanceId.hpp"
#include <string>
#include <vector>

namespace services
{
    // Loading progress for animation preview
    struct AnimationPreviewLoadingProgress
    {
        enum class State
        {
            Idle,
            LoadingMesh,
            LoadingAnimation,
            Complete,
            Failed
        };

        State state = State::Idle;
        float progress = 0.0f;
        std::string statusMessage;
        std::string errorMessage;

        bool isDone() const { return state == State::Complete || state == State::Failed; }
        bool isLoading() const { return state == State::LoadingMesh || state == State::LoadingAnimation; }
    };

    // Evaluated bone transform info for UI display
    struct EvaluatedBoneInfo
    {
        std::string name;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        glm::mat4 worldTransform{1.0f};
        glm::vec3 skinnedPosition{0.0f};  // Final position after skinning (for visualization)
        int32_t parentIndex = -1;
    };

    // Animation playback parameters
    struct AnimationPreviewParams
    {
        glm::mat4 modelMatrix{1.0f};
        glm::vec4 albedo{0.8f, 0.8f, 0.8f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
    };

    class IAnimationPreviewProvider
    {
    public:
        virtual ~IAnimationPreviewProvider() = default;

        // Initialization and cleanup
        virtual void initAnimationPreview(PreviewInstanceId instanceId) = 0;
        virtual void cleanUpAnimationPreview(PreviewInstanceId instanceId) = 0;
        virtual bool isAnimationPreviewInitialized(PreviewInstanceId instanceId) const = 0;

        // Loading
        virtual bool loadAnimationPreviewMesh(PreviewInstanceId instanceId, const std::string& meshPath) = 0;
        virtual bool loadAnimationPreviewAnimation(PreviewInstanceId instanceId, const std::string& animPath) = 0;
        virtual void unloadAnimationPreview(PreviewInstanceId instanceId) = 0;

        // State queries
        virtual bool isAnimationPreviewMeshLoaded(PreviewInstanceId instanceId) const = 0;
        virtual bool isAnimationPreviewAnimationLoaded(PreviewInstanceId instanceId) const = 0;
        virtual math::AABB getAnimationPreviewMeshBounds(PreviewInstanceId instanceId) const = 0;

        // Animation playback control
        virtual void playAnimation(PreviewInstanceId instanceId) = 0;
        virtual void pauseAnimation(PreviewInstanceId instanceId) = 0;
        virtual void stopAnimation(PreviewInstanceId instanceId) = 0;
        virtual bool isAnimationPlaying(PreviewInstanceId instanceId) const = 0;

        virtual void setAnimationPlaybackTime(PreviewInstanceId instanceId, float timeSeconds) = 0;
        virtual float getAnimationPlaybackTime(PreviewInstanceId instanceId) const = 0;
        virtual float getAnimationDuration(PreviewInstanceId instanceId) const = 0;

        virtual void setAnimationLooping(PreviewInstanceId instanceId, bool loop) = 0;
        virtual bool isAnimationLooping(PreviewInstanceId instanceId) const = 0;
        virtual void setAnimationPlaybackSpeed(PreviewInstanceId instanceId, float speed) = 0;
        virtual float getAnimationPlaybackSpeed(PreviewInstanceId instanceId) const = 0;

        // Update (call each frame)
        virtual void updateAnimationPreview(PreviewInstanceId instanceId, float deltaTime) = 0;

        // Parameters
        virtual void setAnimationPreviewParams(PreviewInstanceId instanceId, const AnimationPreviewParams& params) = 0;

        // Camera
        virtual void updateAnimationCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                           const glm::mat4& projection, const glm::vec3& cameraPos) = 0;

        // Render
        virtual void* renderAnimationPreview(PreviewInstanceId instanceId) = 0;

        // Bone info for UI
        virtual size_t getAnimationPreviewBoneCount(PreviewInstanceId instanceId) const = 0;
        virtual std::vector<EvaluatedBoneInfo> getAnimationPreviewEvaluatedBones(PreviewInstanceId instanceId) const = 0;
    };
}
