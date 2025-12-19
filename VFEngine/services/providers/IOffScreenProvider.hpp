#pragma once
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <optional>

namespace services {

    // Camera ID type for multi-camera occlusion culling
    using CameraId = uint32_t;
    constexpr CameraId MAIN_CAMERA_ID = 0;

    struct MeshBounds {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    // Per-camera culling statistics
    struct CameraCullingStats {
        CameraId cameraId = 0;
        bool isActive = false;
        bool occlusionEnabled = false;
        bool occlusionInitialized = false;
        bool frustumReady = false;
        bool bvhBuilt = false;
        uint32_t totalMeshEntities = 0;
        uint32_t visibleAfterFrustumCull = 0;
        uint32_t visibleAfterOcclusionCull = 0;
        uint32_t occludedCount = 0;
    };

    struct CullingDebugStats {
        std::vector<CameraCullingStats> cameraStats;
        CameraId activeCameraId = 0;
    };

    class IOffScreenProvider {
    public:
        virtual ~IOffScreenProvider() = default;

        // Lifecycle
        virtual void init() = 0;
        virtual void cleanUp() = 0;

        virtual void* render() = 0;

        virtual void iblSet(std::string_view iblPath) = 0;
        virtual void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection) = 0;
        virtual void iblRemove() = 0;

        virtual std::string meshLoad(std::string_view meshPath) = 0;
        virtual void meshUnload(const std::string& meshId) = 0;
        // Update camera matrices (main camera - backward compatible)
        virtual void meshUpdateCamera(const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& cameraPos, float time = 0.0f) = 0;
        // Update camera matrices for specific camera
        virtual void meshUpdateCamera(CameraId cameraId, const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& cameraPos, float time = 0.0f) = 0;
        virtual bool isMeshLoaded(const std::string& meshPath) const = 0;
        virtual std::vector<std::string> getLoadedMeshes() const = 0;
        virtual void prepareCameras() = 0;  // Sync CameraComponents with occlusion system
        virtual std::optional<MeshBounds> getMeshBoundingBox(const std::string& meshPath) const = 0;
        virtual void prepareFrameMeshes() = 0;

        // BVH spatial culling
        virtual void rebuildBVH() = 0;
        virtual void markBVHDirty() = 0;

        // Multi-camera occlusion culling
        // Create a secondary camera (e.g., minimap) with optional occlusion culling
        virtual void createCamera(CameraId id, bool enableOcclusion = false) = 0;
        virtual void removeCamera(CameraId id) = 0;
        virtual void setActiveCamera(CameraId id) = 0;
        virtual CameraId getActiveCameraId() const = 0;
        virtual void prepareFrameCameraFrustums() = 0;

        // Billboard API
        virtual void prepareFrameBillboards() = 0;
        virtual void setShowBillboardIcons(bool show) = 0;
        virtual bool getShowBillboardIcons() const = 0;
        virtual bool loadBillboardAtlas(const std::string& atlasPath) = 0;

        // Debug/Stats API
        virtual CullingDebugStats getCullingStats() const = 0;
    };

}
