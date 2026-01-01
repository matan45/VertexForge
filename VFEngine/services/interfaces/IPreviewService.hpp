#pragma once
#include "../providers/IPreviewProvider.hpp"
#include "../data/DTOs.hpp"
#include "../data/AsyncLoadingTypes.hpp"
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
        
        [[nodiscard]] virtual bool isMaterialPreviewReady(PreviewInstanceId instanceId) const = 0;

        virtual void setMaterialParams(PreviewInstanceId instanceId, const MaterialPreviewParams& params) = 0;

        [[nodiscard]] virtual MaterialPreviewParams getMaterialParams(PreviewInstanceId instanceId) const = 0;

        virtual void updateMaterialCamera(PreviewInstanceId instanceId, const glm::mat4& view,
                                          const glm::mat4& projection,
                                          const glm::vec3& cameraPos, float time = 0.0f) = 0;

        [[nodiscard]] virtual ViewportTextureHandle renderMaterialPreview(PreviewInstanceId instanceId) = 0;

        [[nodiscard]] virtual std::string getMaterialShaderError(PreviewInstanceId instanceId) const = 0;

        virtual void initMeshPreview(PreviewInstanceId instanceId) = 0;

        virtual void cleanUpMeshPreview(PreviewInstanceId instanceId) = 0;

        [[nodiscard]] virtual bool isMeshPreviewReady(PreviewInstanceId instanceId) const = 0;

        [[nodiscard]] virtual bool isPreviewMeshLoaded(PreviewInstanceId instanceId) const = 0;


        [[nodiscard]] virtual std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const =
        0;
        
        [[nodiscard]] virtual std::vector<LODInfo> getPreviewMeshLODInfo(PreviewInstanceId instanceId) const = 0;
        
        [[nodiscard]] virtual math::AABB getPreviewMeshBounds(PreviewInstanceId instanceId) const = 0;
        
        virtual void setMeshPreviewParams(PreviewInstanceId instanceId, const MeshPreviewParams& params) = 0;

        virtual void updateMeshCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                      const glm::vec3& cameraPos) = 0;

        [[nodiscard]] virtual ViewportTextureHandle renderMeshPreview(PreviewInstanceId instanceId) = 0;

        // Async mesh loading
        virtual void loadPreviewMeshAsync(PreviewInstanceId instanceId, const std::string& meshPath) = 0;
        virtual void cancelMeshLoading(PreviewInstanceId instanceId) = 0;
        [[nodiscard]] virtual MeshLoadingProgress getMeshLoadingProgress(PreviewInstanceId instanceId) const = 0;
        virtual void processAsyncLoading() = 0;
    };
}
