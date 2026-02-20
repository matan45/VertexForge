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

    struct MeshBounds {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

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

    struct GPUDrivenDebugStats {
        bool enabled = false;
        bool frustumCullingEnabled = true;
        bool occlusionCullingEnabled = true;
        bool lodSelectionEnabled = false;
        uint32_t hiZMipLevels = 0;

        uint32_t totalObjects = 0;
        uint32_t visibleObjects = 0;
        uint32_t culledByFrustum = 0;
        uint32_t culledByOcclusion = 0;

        uint32_t objectsLOD0 = 0;
        uint32_t objectsLOD1 = 0;
        uint32_t objectsLOD2 = 0;
        uint32_t objectsLOD3 = 0;

        uint32_t mergedVertexCount = 0;
        uint32_t mergedIndexCount = 0;
        uint32_t registeredMeshCount = 0;
        uint32_t registeredTextureCount = 0;

        uint32_t batchCount = 0;
        uint32_t commandsPerBatch = 0;
        uint32_t totalCapacity = 0;
        uint32_t drawCalls = 0;

        uint64_t drawCommandBufferSize = 0;
        uint64_t drawCountBufferSize = 0;
        uint64_t perDrawDataBufferSize = 0;
        uint64_t totalMemoryUsage = 0;

        bool meshletFrustumCullingEnabled = false;
        bool meshletBackfaceCullingEnabled = false;
        uint32_t totalMeshlets = 0;
        uint32_t meshletsCulledByFrustum = 0;
        uint32_t meshletsCulledByBackface = 0;
        uint32_t visibleMeshlets = 0;

        bool bvhLightCullingEnabled = false;
        bool hiZLightOcclusionEnabled = false;
        uint32_t totalLights = 0;
        uint32_t lightsAfterBVHCull = 0;
        uint32_t lightsAfterHiZCull = 0;
        uint32_t lightsCulledByBVH = 0;
        uint32_t lightsCulledByHiZ = 0;
    };

    struct TerrainDebugStats {
        float updateTerrainUs = 0.0f;
        float streamingUs = 0.0f;
        float buildTileDataUs = 0.0f;
        float uploadTileDataUs = 0.0f;

        uint32_t totalTiles = 0;
        uint32_t culledTiles = 0;
        uint32_t totalMeshlets = 0;
        uint32_t culledMeshlets = 0;
        uint32_t visibleMeshlets = 0;
        uint32_t lodCount0 = 0;
        uint32_t lodCount1 = 0;
        uint32_t lodCount2 = 0;
        uint32_t lodCount3 = 0;

        uint32_t tilesLoaded = 0;
        uint32_t tilesStreaming = 0;
        uint32_t fallbackTiles = 0;
        uint32_t fullDetailTiles = 0;
        uint32_t uploadsThisFrame = 0;
        size_t memoryUsedBytes = 0;
        size_t memoryBudgetBytes = 0;
        size_t bytesUploadedThisFrame = 0;
    };

    struct CullingDebugStats {
        std::vector<CameraCullingStats> cameraStats;
        CameraId activeCameraId = 0;

        size_t staticBvhEntityCount = 0;
        size_t dynamicBvhEntityCount = 0;
        size_t staticBvhNodeCount = 0;
        size_t dynamicBvhNodeCount = 0;

        size_t staticLightBvhCount = 0;
        size_t dynamicLightBvhCount = 0;
        size_t staticLightBvhNodeCount = 0;
        size_t dynamicLightBvhNodeCount = 0;

        GPUDrivenDebugStats gpuDriven;
        TerrainDebugStats terrain;
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
        uint32_t pointResolution = 512;
    };

    class IOffScreenProvider {
    public:
        virtual ~IOffScreenProvider() = default;

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
        virtual void prepareCameras() = 0;
        virtual std::optional<MeshBounds> getMeshBoundingBox(const std::string& meshPath) const = 0;
        virtual void prepareFrameMeshes() = 0;

        virtual void removeCamera(CameraId id) = 0;
        virtual void prepareFrameCameraFrustums() = 0;
        virtual void prepareFrameAudioSpheres() = 0;
        virtual void prepareFrameLightGizmos() = 0;

        virtual void prepareFrameBillboards() = 0;
        virtual void prepareFrameText() = 0;
        virtual void setShowBillboardIcons(bool show) = 0;
        virtual bool getShowBillboardIcons() const = 0;
        virtual bool loadBillboardAtlas(const std::string& atlasPath) = 0;

        virtual CullingDebugStats getCullingStats() const = 0;

        virtual void applyShadowSettings(const types::RenderSettings& settings) = 0;
        virtual ShadowStats getShadowStats() const = 0;

        virtual void setPlayMode(bool playMode) = 0;

        virtual void setShowDebugRendering(bool show) = 0;
        virtual bool getShowDebugRendering() const = 0;

        virtual void setShowGrid(bool show) = 0;
        virtual bool getShowGrid() const = 0;
        virtual void prepareGrid() = 0;

        virtual void setShowPhysicsDebug(bool show) = 0;
        virtual bool getShowPhysicsDebug() const = 0;
        virtual void prepareFramePhysicsColliders() = 0;

        virtual void setViewMode(uint32_t mode) = 0;
        virtual uint32_t getViewMode() const = 0;

        virtual void setShowClusterDebug(bool show) = 0;
        virtual bool getShowClusterDebug() const = 0;
        virtual void prepareFrameClusterDebug() = 0;

        virtual void setShowShadowDebug(bool show) = 0;
        virtual bool getShowShadowDebug() const = 0;
        virtual void prepareFrameShadowDebug() = 0;

        virtual void setShowNavmeshDebug(bool show) = 0;
        virtual bool getShowNavmeshDebug() const = 0;
        virtual void updateNavmeshDebugMesh(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices) = 0;
        virtual void clearNavmeshDebugMesh() = 0;

        virtual void prepareFrameUICanvasOutlines() = 0;
        virtual void prepareFrameUIImages() = 0;

        virtual void setFrustumCullingEnabled(bool enabled) = 0;
        virtual void setOcclusionCullingEnabled(bool enabled) = 0;
        virtual void setLODSelectionEnabled(bool enabled) = 0;
        virtual void setMeshletFrustumCullingEnabled(bool enabled) = 0;
        virtual void setMeshletBackfaceCullingEnabled(bool enabled) = 0;

        virtual void setTerrainFrustumCullingEnabled(bool enabled) = 0;
        virtual void setTerrainMeshletCullingEnabled(bool enabled) = 0;

        virtual void setTerrainRenderingEnabled(bool enabled) = 0;
        virtual void setTerrainLODBias(float bias) = 0;
        virtual void setTerrainErrorThreshold(float threshold) = 0;
        virtual void setTerrainTextureScale(float scale) = 0;
        virtual void setTerrainShadowLOD(uint32_t lod) = 0;

        virtual void setUIViewportOffset(const glm::vec2& offset, const glm::vec2& panelSize) = 0;
    };

}

