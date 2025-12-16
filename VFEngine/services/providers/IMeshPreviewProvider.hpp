#pragma once
#include <glm/glm.hpp>
#include <math/Frustum.hpp>
#include "../data/DTOs.hpp"
#include "PreviewInstanceId.hpp"
#include <string>
#include <vector>

namespace services {

    struct MeshPreviewParams {
        glm::mat4 modelMatrix{ 1.0f };
        int highlightedSubMesh = -1;  // -1 = none highlighted
    };

    class IMeshPreviewProvider {
    public:
        virtual ~IMeshPreviewProvider() = default;

        virtual void initMeshPreview(PreviewInstanceId instanceId) = 0;

        virtual void cleanUpMeshPreview(PreviewInstanceId instanceId) = 0;

        virtual bool isMeshPreviewInitialized(PreviewInstanceId instanceId) const = 0;

        virtual bool loadPreviewMesh(PreviewInstanceId instanceId, const std::string& meshPath, math::AABB& outBounds) = 0;

        virtual void unloadPreviewMesh(PreviewInstanceId instanceId) = 0;

        virtual bool isPreviewMeshLoaded(PreviewInstanceId instanceId) const = 0;

        virtual std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const = 0;

        virtual math::AABB getPreviewMeshBounds(PreviewInstanceId instanceId) const = 0;

        virtual void setMeshPreviewParams(PreviewInstanceId instanceId, const MeshPreviewParams& params) = 0;

        virtual void updateMeshCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& cameraPos) = 0;

        virtual void* renderMeshPreview(PreviewInstanceId instanceId) = 0;
    };

}
