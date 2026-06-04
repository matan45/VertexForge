#pragma once
#include "../../providers/render/IPreviewProvider.hpp"
#include "../../providers/animation/IAnimationPreviewProvider.hpp"
#include "../../providers/vfx/IVFXPreviewProvider.hpp"
#include "../../data/DTOs.hpp"
#include "../../data/AsyncLoadingTypes.hpp"
#include <math/Frustum.hpp>
#include <string>
#include <vector>

namespace services
{
    class IPreviewService
    {
    public:
        virtual ~IPreviewService() = default;

        virtual void registerEventHandlers() = 0;

        virtual void initMaterialPreview(PreviewInstanceId instanceId) = 0;

        virtual void cleanUpMaterialPreview(PreviewInstanceId instanceId) = 0;

        virtual void setMaterialParams(PreviewInstanceId instanceId, const MaterialPreviewParams& params) = 0;

        virtual void updateMaterialCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                          const glm::mat4& projection,
                                          const glm::vec3& cameraPos, float time = 0.0f) = 0;

        [[nodiscard]] virtual ViewportTextureHandle renderMaterialPreview(PreviewInstanceId instanceId) = 0;

        [[nodiscard]] virtual std::string getMaterialShaderError(PreviewInstanceId instanceId) const = 0;

        virtual void initMeshPreview(PreviewInstanceId instanceId) = 0;

        virtual void cleanUpMeshPreview(PreviewInstanceId instanceId) = 0;

        [[nodiscard]] virtual std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const =
        0;

        [[nodiscard]] virtual std::vector<LODInfo> getPreviewMeshLODInfo(PreviewInstanceId instanceId) const = 0;

        [[nodiscard]] virtual math::AABB getPreviewMeshBounds(PreviewInstanceId instanceId) const = 0;

        virtual void setMeshPreviewParams(PreviewInstanceId instanceId, const MeshPreviewParams& params) = 0;

        virtual void setPreviewEnvironment(PreviewInstanceId instanceId, const PreviewEnvironmentParams& params) = 0;

        virtual void updateMeshCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                      const glm::vec3& cameraPos) = 0;

        [[nodiscard]] virtual ViewportTextureHandle renderMeshPreview(PreviewInstanceId instanceId) = 0;

        // Async mesh loading
        virtual void loadPreviewMeshAsync(PreviewInstanceId instanceId, const std::string& meshPath) = 0;
        virtual void cancelMeshLoading(PreviewInstanceId instanceId) = 0;
        [[nodiscard]] virtual MeshLoadingProgress getMeshLoadingProgress(PreviewInstanceId instanceId) const = 0;
        virtual void processAsyncLoading() = 0;

        // Animation Preview
        virtual void initAnimationPreview(PreviewInstanceId instanceId) = 0;
        virtual void cleanUpAnimationPreview(PreviewInstanceId instanceId) = 0;
        virtual bool loadAnimationPreviewMesh(PreviewInstanceId instanceId, const std::string& meshPath) = 0;
        virtual bool loadAnimationPreviewAnimation(PreviewInstanceId instanceId, const std::string& animPath) = 0;

        virtual void playAnimation(PreviewInstanceId instanceId) = 0;
        virtual void pauseAnimation(PreviewInstanceId instanceId) = 0;
        virtual void stopAnimation(PreviewInstanceId instanceId) = 0;
        [[nodiscard]] virtual bool isAnimationPlaying(PreviewInstanceId instanceId) const = 0;

        virtual void setAnimationPlaybackTime(PreviewInstanceId instanceId, float timeSeconds) = 0;
        [[nodiscard]] virtual float getAnimationPlaybackTime(PreviewInstanceId instanceId) const = 0;

        virtual void setAnimationLooping(PreviewInstanceId instanceId, bool loop) = 0;
        virtual void setAnimationPlaybackSpeed(PreviewInstanceId instanceId, float speed) = 0;

        virtual void updateAnimationPreview(PreviewInstanceId instanceId, float deltaTime) = 0;
        virtual void setAnimationPreviewParams(PreviewInstanceId instanceId, const AnimationPreviewParams& params) = 0;
        virtual void setAnimationPreviewEnvironment(PreviewInstanceId instanceId,
                                                    const PreviewEnvironmentParams& params) = 0;
        virtual void updateAnimationCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                           const glm::mat4& projection, const glm::vec3& cameraPos) = 0;

        [[nodiscard]] virtual ViewportTextureHandle renderAnimationPreview(PreviewInstanceId instanceId) = 0;
        [[nodiscard]] virtual std::vector<EvaluatedBoneInfo>
        getAnimationPreviewEvaluatedBones(PreviewInstanceId instanceId) const = 0;

        // VFX Preview
        virtual void initVFXPreview(PreviewInstanceId instanceId) = 0;
        virtual void cleanUpVFXPreview(PreviewInstanceId instanceId) = 0;

        virtual void setVFXParams(PreviewInstanceId instanceId, const VFXPreviewParams& params) = 0;

        virtual void updateVFXCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                     const glm::mat4& projection, const glm::vec3& cameraPos, float time) = 0;
        virtual void updateVFXSimulation(PreviewInstanceId instanceId, float deltaTime) = 0;

        virtual void playVFX(PreviewInstanceId instanceId) = 0;
        virtual void pauseVFX(PreviewInstanceId instanceId) = 0;
        virtual void stopVFX(PreviewInstanceId instanceId) = 0;

        [[nodiscard]] virtual ViewportTextureHandle renderVFXPreview(PreviewInstanceId instanceId) = 0;
    };
}
