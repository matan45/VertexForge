#pragma once
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <optional>
#include "types/CameraTypes.hpp"
#include "types/RenderSettings.hpp"

namespace services {

    using types::CameraId;
    using types::MAIN_CAMERA_ID;

    class IVFXRuntimeProvider;

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

    // GPU-driven rendering statistics
    struct GPUDrivenDebugStats {
        bool enabled = false;
        bool frustumCullingEnabled = false;
        bool occlusionCullingEnabled = false;
        bool lodSelectionEnabled = false;
        uint32_t hiZMipLevels = 0;

        uint32_t totalObjects = 0;
        uint32_t visibleObjects = 0;
        uint32_t culledByFrustum = 0;
        uint32_t culledByOcclusion = 0;

        // LOD distribution
        uint32_t objectsLOD0 = 0;
        uint32_t objectsLOD1 = 0;
        uint32_t objectsLOD2 = 0;
        uint32_t objectsLOD3 = 0;

        // Merged buffer stats
        uint32_t mergedVertexCount = 0;
        uint32_t mergedIndexCount = 0;
        uint32_t registeredMeshCount = 0;
        uint32_t registeredTextureCount = 0;

        // Batch rendering stats
        uint32_t batchCount = 0;
        uint32_t commandsPerBatch = 0;
        uint32_t totalCapacity = 0;
        uint32_t drawCalls = 0;

        // Memory usage (in bytes)
        uint64_t drawCommandBufferSize = 0;
        uint64_t drawCountBufferSize = 0;
        uint64_t perDrawDataBufferSize = 0;
        uint64_t totalMemoryUsage = 0;

        // Meshlet culling stats (from task shader)
        bool meshletFrustumCullingEnabled = false;
        bool meshletBackfaceCullingEnabled = false;
        uint32_t totalMeshlets = 0;
        uint32_t meshletsCulledByFrustum = 0;
        uint32_t meshletsCulledByBackface = 0;
        uint32_t visibleMeshlets = 0;

        // Light culling stats
        bool bvhLightCullingEnabled = false;
        bool hiZLightOcclusionEnabled = false;
        uint32_t totalLights = 0;              // Total point + spot lights in scene
        uint32_t lightsAfterBVHCull = 0;       // Lights visible after BVH frustum cull
        uint32_t lightsAfterHiZCull = 0;       // Lights visible after Hi-Z occlusion
        uint32_t lightsCulledByBVH = 0;        // Lights culled by BVH frustum
        uint32_t lightsCulledByHiZ = 0;        // Lights culled by Hi-Z occlusion
    };

    struct CullingDebugStats {
        std::vector<CameraCullingStats> cameraStats;
        CameraId activeCameraId = 0;

        // Mesh BVH statistics
        size_t staticBvhEntityCount = 0;
        size_t dynamicBvhEntityCount = 0;
        size_t staticBvhNodeCount = 0;
        size_t dynamicBvhNodeCount = 0;

        // Light BVH statistics
        size_t staticLightBvhCount = 0;
        size_t dynamicLightBvhCount = 0;
        size_t staticLightBvhNodeCount = 0;
        size_t dynamicLightBvhNodeCount = 0;

        // GPU-driven rendering statistics
        GPUDrivenDebugStats gpuDriven;
    };

    struct ShadowStats {
        uint32_t atlasWidth = 0;
        uint32_t atlasHeight = 0;
        float atlasUtilization = 0.0f;
        uint32_t activeShadowCasters = 0;
        uint32_t activeShadowViews = 0;
        uint32_t directionalLightCount = 0;
        uint32_t pointLightCount = 0;
        uint32_t spotLightCount = 0;
        uint32_t pointResolution = 512;  // Per-face resolution for VRAM calculation
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
        virtual void prepareFrameAudioSpheres() = 0;
        virtual void prepareFrameLightGizmos() = 0;

        // Billboard API
        virtual void prepareFrameBillboards() = 0;
        virtual void setShowBillboardIcons(bool show) = 0;
        virtual bool getShowBillboardIcons() const = 0;
        virtual bool loadBillboardAtlas(const std::string& atlasPath) = 0;

        // Debug/Stats API
        virtual CullingDebugStats getCullingStats() const = 0;

        // Shadow Settings API
        virtual void applyShadowSettings(const types::RenderSettings& settings) = 0;
        virtual ShadowStats getShadowStats() const = 0;

        // Editor Mode API
        virtual void setPlayMode(bool playMode) = 0;
        virtual bool isPlayMode() const = 0;

        // Debug Rendering API
        virtual void setShowDebugRendering(bool show) = 0;
        virtual bool getShowDebugRendering() const = 0;

        // Grid API
        virtual void setShowGrid(bool show) = 0;
        virtual bool getShowGrid() const = 0;
        virtual void prepareGrid() = 0;

        // Physics Debug API
        virtual void setShowPhysicsDebug(bool show) = 0;
        virtual bool getShowPhysicsDebug() const = 0;
        virtual void prepareFramePhysicsColliders() = 0;

        // View Mode API
        virtual void setViewMode(uint32_t mode) = 0;
        virtual uint32_t getViewMode() const = 0;

        // Cluster Debug API
        virtual void setShowClusterDebug(bool show) = 0;
        virtual bool getShowClusterDebug() const = 0;
        virtual void prepareFrameClusterDebug() = 0;

        // Shadow Debug API
        virtual void setShowShadowDebug(bool show) = 0;
        virtual bool getShowShadowDebug() const = 0;
        virtual void prepareFrameShadowDebug() = 0;

        // VFX Runtime API
        virtual void setVFXRuntimeProvider(IVFXRuntimeProvider* provider) = 0;
    };

}
