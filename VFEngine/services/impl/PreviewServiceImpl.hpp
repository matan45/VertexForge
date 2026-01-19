#pragma once
#include "../interfaces/IPreviewService.hpp"
#include "../events/PreviewEvents.hpp"
#include "../events/AnimationPreviewEvents.hpp"

namespace services
{
    class IMaterialPreviewProvider;
    class IMeshPreviewProvider;
    class IAnimationPreviewProvider;


    class PreviewServiceImpl : public IPreviewService
    {
    private:
        IMaterialPreviewProvider* materialProvider;
        IMeshPreviewProvider* meshProvider;
        IAnimationPreviewProvider* animationProvider;

    public:
        explicit PreviewServiceImpl(IMaterialPreviewProvider* materialProvider, IMeshPreviewProvider* meshProvider,
                                    IAnimationPreviewProvider* animationProvider = nullptr);
        ~PreviewServiceImpl() override;

        void registerEventHandlers() override;

        void initMaterialPreview(PreviewInstanceId instanceId) override;
        void cleanUpMaterialPreview(PreviewInstanceId instanceId) override;
        void setMaterialParams(PreviewInstanceId instanceId, const MaterialPreviewParams& params) override;
        void updateMaterialCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& cameraPos, float time = 0.0f) override;
        [[nodiscard]] ViewportTextureHandle renderMaterialPreview(PreviewInstanceId instanceId) override;
        [[nodiscard]] std::string getMaterialShaderError(PreviewInstanceId instanceId) const override;

        void initMeshPreview(PreviewInstanceId instanceId) override;
        void cleanUpMeshPreview(PreviewInstanceId instanceId) override;
        [[nodiscard]] std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const override;
        [[nodiscard]] std::vector<LODInfo> getPreviewMeshLODInfo(PreviewInstanceId instanceId) const override;
        [[nodiscard]] math::AABB getPreviewMeshBounds(PreviewInstanceId instanceId) const override;
        void setMeshPreviewParams(PreviewInstanceId instanceId, const MeshPreviewParams& params) override;
        void updateMeshCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos) override;
        [[nodiscard]] ViewportTextureHandle renderMeshPreview(PreviewInstanceId instanceId) override;

        void loadPreviewMeshAsync(PreviewInstanceId instanceId, const std::string& meshPath) override;
        void cancelMeshLoading(PreviewInstanceId instanceId) override;
        [[nodiscard]] MeshLoadingProgress getMeshLoadingProgress(PreviewInstanceId instanceId) const override;
        void processAsyncLoading() override;

        void initAnimationPreview(PreviewInstanceId instanceId) override;
        void cleanUpAnimationPreview(PreviewInstanceId instanceId) override;
        bool loadAnimationPreviewMesh(PreviewInstanceId instanceId, const std::string& meshPath) override;
        bool loadAnimationPreviewAnimation(PreviewInstanceId instanceId, const std::string& animPath) override;

        void playAnimation(PreviewInstanceId instanceId) override;
        void pauseAnimation(PreviewInstanceId instanceId) override;
        void stopAnimation(PreviewInstanceId instanceId) override;
        [[nodiscard]] bool isAnimationPlaying(PreviewInstanceId instanceId) const override;

        void setAnimationPlaybackTime(PreviewInstanceId instanceId, float timeSeconds) override;
        [[nodiscard]] float getAnimationPlaybackTime(PreviewInstanceId instanceId) const override;

        void setAnimationLooping(PreviewInstanceId instanceId, bool loop) override;
        void setAnimationPlaybackSpeed(PreviewInstanceId instanceId, float speed) override;

        void updateAnimationPreview(PreviewInstanceId instanceId, float deltaTime) override;
        void setAnimationPreviewParams(PreviewInstanceId instanceId, const AnimationPreviewParams& params) override;
        void updateAnimationCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                   const glm::mat4& projection, const glm::vec3& cameraPos) override;

        [[nodiscard]] ViewportTextureHandle renderAnimationPreview(PreviewInstanceId instanceId) override;
        [[nodiscard]] std::vector<EvaluatedBoneInfo>
        getAnimationPreviewEvaluatedBones(PreviewInstanceId instanceId) const override;
    };
}
