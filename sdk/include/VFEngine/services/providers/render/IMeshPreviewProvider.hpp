#pragma once
#include <glm/glm.hpp>
#include <math/Frustum.hpp>
#include "../../data/DTOs.hpp"
#include "../../data/AsyncLoadingTypes.hpp"
#include "../PreviewInstanceId.hpp"
#include <string>
#include <vector>

namespace services {

    struct PreviewEnvironmentParams {
        uint8_t backgroundMode = 0;   // 0=solid, 1=gradient
        glm::vec4 backgroundColor{ 0.15f, 0.15f, 0.15f, 1.0f };
        glm::vec4 gradientTopColor{ 0.165f, 0.184f, 0.271f, 1.0f };
        glm::vec4 gradientBottomColor{ 0.106f, 0.118f, 0.169f, 1.0f };
        bool showGrid = true;
        uint8_t lightingMode = 0;     // 0=default/IBL-only, 1=three-point
        float lightingIntensity = 1.0f;

        // VK-1433 Phase 1 — prefab rig debug overlays (skeleton/sockets/IK targets). Read only
        // by PrefabRigPreviewController; other preview controllers ignore them.
        bool showSkeleton = false;
        bool showSockets = false;
        bool showIKTargets = false;

        // VK-1433 Phase 1c — the socket the editor currently has selected, so the overlay can draw
        // it distinctly (a highlight halo) and the user can tell which triad they are editing.
        // (part index, socket index within that part); -1/-1 = none. Only read by
        // PrefabRigPreviewController's socket overlay loop; other preview controllers ignore them.
        int highlightedSocketPart = -1;
        int highlightedSocketIndex = -1;

        // VK-1433 Phase 3 — debug shading mode for the prefab rig preview. 0=none (unchanged PBR),
        // 1=clay, 2=normals, 3=UVs, 4=albedo-unlit, 5=wireframe. Read only by
        // PrefabRigPreviewController; default 0 keeps every other preview controller unchanged.
        uint8_t shadingMode = 0;

        // VK-1433 Phase 3 — primary (key) light direction for three-point lighting, in spherical
        // editor coordinates (radians). Azimuth around +Y, elevation above the XZ plane. Only read
        // when lightingMode == 1. Default points down-and-forward, a flattering key angle.
        float lightAzimuth = 0.6f;
        float lightElevation = 0.6f;
    };

    struct MeshPreviewParams {
        glm::mat4 modelMatrix{ 1.0f };
        int highlightedSubMesh = -1;  // -1 = none highlighted
        int forceLODLevel = -1;       // -1 = auto LOD selection, 0-3 = force specific LOD
        bool wireframeMode = false;
        bool showBoundingBox = false;
        int materialOverrideMode = 0; // 0=default, 1=clay, 2=normals, 3=UVs
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

        virtual void setPreviewEnvironment(PreviewInstanceId instanceId, const PreviewEnvironmentParams& params) = 0;

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
