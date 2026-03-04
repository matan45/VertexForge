#pragma once

#include "providers/IAnimationPreviewProvider.hpp"
#include "../../graphics/controllers/preview/AnimatedMeshPreviewController.hpp"
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

        void initAnimationPreview(services::PreviewInstanceId instanceId) override;
        void cleanUpAnimationPreview(services::PreviewInstanceId instanceId) override;
        bool loadAnimationPreviewMesh(services::PreviewInstanceId instanceId,
                                      const std::string& meshPath) override;
        bool loadAnimationPreviewAnimation(services::PreviewInstanceId instanceId,
                                           const std::string& animPath) override;

        void playAnimation(services::PreviewInstanceId instanceId) override;
        void pauseAnimation(services::PreviewInstanceId instanceId) override;
        void stopAnimation(services::PreviewInstanceId instanceId) override;
        bool isAnimationPlaying(services::PreviewInstanceId instanceId) const override;

        void setAnimationPlaybackTime(services::PreviewInstanceId instanceId, float timeSeconds) override;
        float getAnimationPlaybackTime(services::PreviewInstanceId instanceId) const override;

        void setAnimationLooping(services::PreviewInstanceId instanceId, bool loop) override;
        void setAnimationPlaybackSpeed(services::PreviewInstanceId instanceId, float speed) override;

        void updateAnimationPreview(services::PreviewInstanceId instanceId, float deltaTime) override;
        void setAnimationPreviewParams(services::PreviewInstanceId instanceId,
                                       const services::AnimationPreviewParams& params) override;
        void updateAnimationCamera(services::PreviewInstanceId instanceId, const glm::mat4& view,
                                   const glm::mat4& projection, const glm::vec3& cameraPos) override;

        void* renderAnimationPreview(services::PreviewInstanceId instanceId) override;
        std::vector<services::EvaluatedBoneInfo>
        getAnimationPreviewEvaluatedBones(services::PreviewInstanceId instanceId) const override;

    private:
        controllers::AnimatedMeshPreviewController* getController(services::PreviewInstanceId instanceId) const;
    };
}
