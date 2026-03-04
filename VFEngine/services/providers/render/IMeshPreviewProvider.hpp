#pragma once
#include <glm/glm.hpp>
#include <math/Frustum.hpp>
#include "../../data/DTOs.hpp"
#include "../../data/AsyncLoadingTypes.hpp"
#include "../PreviewInstanceId.hpp"
#include <string>
#include <vector>

namespace services {

    struct MeshPreviewParams {
        glm::mat4 modelMatrix{ 1.0f };
        int highlightedSubMesh = -1;  // -1 = none highlighted
        int forceLODLevel = -1;       // -1 = auto LOD selection, 0-3 = force specific LOD
    };

    class IMeshPreviewProvider {
    public:
        virtual ~IMeshPreviewProvider() = default;

        virtual void initMeshPreview(PreviewInstanceId instanceId) = 0;

        virtual void cleanUpMeshPreview(PreviewInstanceId instanceId) = 0;

        virtual bool isMeshPreviewInitialized(PreviewInstanceId instanceId) const = 0;

        virtual bool isPreviewMeshLoaded(PreviewInstanceId instanceId) const = 0;

        virtual std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const = 0;

        virtual std::vector<LODInfo> getPreviewMeshLODInfo(PreviewInstanceId instanceId) const = 0;

        virtual math::AABB getPreviewMeshBounds(PreviewInstanceId instanceId) const = 0;

        virtual void setMeshPreviewParams(PreviewInstanceId instanceId, const MeshPreviewParams& params) = 0;

        virtual void updateMeshCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& cameraPos) = 0;

        virtual void* renderMeshPreview(PreviewInstanceId instanceId) = 0;

        // Async loading API
        virtual void loadPreviewMeshAsync(PreviewInstanceId instanceId, const std::string& meshPath) = 0;
        virtual void cancelMeshLoading(PreviewInstanceId instanceId) = 0;
        virtual MeshLoadingProgress getMeshLoadingProgress(PreviewInstanceId instanceId) const = 0;

        // Call each frame to process async loading work for all instances
        virtual void processAsyncLoading() = 0;
    };

}
