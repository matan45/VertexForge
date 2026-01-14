#pragma once

#include "../../services/providers/IAnimationPreviewProvider.hpp"
#include "../../graphics/controllers/AnimatedMeshPreviewController.hpp"
#include <memory>
#include <unordered_map>

namespace core
{
    class AnimationPreviewAdapter : public services::IAnimationPreviewProvider
    {
    private:
        std::unordered_map<services::PreviewInstanceId, std::unique_ptr<::controllers::AnimatedMeshPreviewController>>
            controllers;

    public:
        explicit AnimationPreviewAdapter() = default;
        ~AnimationPreviewAdapter() noexcept override;

        // Initialization and cleanup
        void initAnimationPreview(services::PreviewInstanceId instanceId) override;
        void cleanUpAnimationPreview(services::PreviewInstanceId instanceId) override;
        bool isAnimationPreviewInitialized(services::PreviewInstanceId instanceId) const override;

        // Loading
        bool loadAnimationPreviewMesh(services::PreviewInstanceId instanceId, const std::string& meshPath) override;
        bool loadAnimationPreviewAnimation(services::PreviewInstanceId instanceId, const std::string& animPath) override;
        void unloadAnimationPreview(services::PreviewInstanceId instanceId) override;

        // State queries
        bool isAnimationPreviewMeshLoaded(services::PreviewInstanceId instanceId) const override;
        bool isAnimationPreviewAnimationLoaded(services::PreviewInstanceId instanceId) const override;
        math::AABB getAnimationPreviewMeshBounds(services::PreviewInstanceId instanceId) const override;

        // Animation playback control
        void playAnimation(services::PreviewInstanceId instanceId) override;
        void pauseAnimation(services::PreviewInstanceId instanceId) override;
        void stopAnimation(services::PreviewInstanceId instanceId) override;
        bool isAnimationPlaying(services::PreviewInstanceId instanceId) const override;

        void setAnimationPlaybackTime(services::PreviewInstanceId instanceId, float timeSeconds) override;
        float getAnimationPlaybackTime(services::PreviewInstanceId instanceId) const override;
        float getAnimationDuration(services::PreviewInstanceId instanceId) const override;

        void setAnimationLooping(services::PreviewInstanceId instanceId, bool loop) override;
        bool isAnimationLooping(services::PreviewInstanceId instanceId) const override;
        void setAnimationPlaybackSpeed(services::PreviewInstanceId instanceId, float speed) override;
        float getAnimationPlaybackSpeed(services::PreviewInstanceId instanceId) const override;

        // Update
        void updateAnimationPreview(services::PreviewInstanceId instanceId, float deltaTime) override;

        // Parameters
        void setAnimationPreviewParams(services::PreviewInstanceId instanceId,
                                        const services::AnimationPreviewParams& params) override;

        // Camera
        void updateAnimationCamera(services::PreviewInstanceId instanceId, const glm::mat4& view,
                                   const glm::mat4& projection, const glm::vec3& cameraPos) override;

        // Render
        void* renderAnimationPreview(services::PreviewInstanceId instanceId) override;

        // Bone info
        size_t getAnimationPreviewBoneCount(services::PreviewInstanceId instanceId) const override;
        std::vector<services::EvaluatedBoneInfo>
            getAnimationPreviewEvaluatedBones(services::PreviewInstanceId instanceId) const override;

    private:
        controllers::AnimatedMeshPreviewController* getController(services::PreviewInstanceId instanceId) const;
    };
}
