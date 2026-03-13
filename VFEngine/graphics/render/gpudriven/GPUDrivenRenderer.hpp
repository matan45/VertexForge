#pragma once

#include "GPUDrivenTypes.hpp"
#include "scene/MergedMeshBuffer.hpp"
#include "scene/IndirectBatchManager.hpp"
#include "scene/BindlessTextureManager.hpp"
#include "scene/GPUCullLODPipeline.hpp"
#include "GPUDrivenCameraBuffer.hpp"
#include "scene/MeshShaderPipeline.hpp"
#include "terrain/TerrainMeshShaderPipeline.hpp"
#include "terrain/TerrainMeshBuffer.hpp"
#include "terrain/TerrainGPUAdapter.hpp"
#include "terrain/TerrainStreamManager.hpp"
#include "../water/WaterPipeline.hpp"
#include "../water/WaterMeshBuffer.hpp"
#include "../water/WaterGPUTypes.hpp"
#include "../water/OceanFFT.hpp"
#include "scene/MeshletBuffer.hpp"
#include "scene/BoneMatrixManager.hpp"
#include "../lighting/GPULightBufferManager.hpp"
#include "../lighting/ClusterGridManager.hpp"
#include "../lighting/LightCullingPipeline.hpp"
#include "../shadow/ShadowSystem.hpp"
#include "../vegetation/WindSystem.hpp"
#include "../vegetation/VegetationBufferManager.hpp"
#include "billboard/BillboardBufferManager.hpp"
#include "billboard/BillboardMeshShaderPipeline.hpp"
#include "billboard/BillboardGPUTypes.hpp"
#include "billboard/BillboardStreamManager.hpp"
#include "vegetation/GrassConfig.hpp"
#include "../vegetation/GrassStreamManager.hpp"
#include "../occlusion/LightOcclusionCulling.hpp"
#include "../volumetric/VolumetricPipeline.hpp"
#include "../lighting/LightStreamManager.hpp"
#include "../gi/GITypes.hpp"
#include "../gi/RadianceCascadeManager.hpp"
#include "../gi/ProbeTracePipeline.hpp"
#include "../gi/ProbeUpdatePipeline.hpp"
#include "../gi/GIDebugRenderer.hpp"
#include "../gi/AccelerationStructureManager.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "../../core/Texture.hpp"
#include "material/MaterialManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace core
{
    class Device;
    class SwapChain;
    class DeferredDeletionQueue;
}

namespace material
{
    struct MaterialData;
}

namespace render::mesh
{
    class MaterialTextureCache;
    class MeshStreamManager;
    struct MeshRenderData;
}

namespace render::occlusion
{
    class HiZBuffer;
}

namespace terrain
{
    class TerrainTile;
}

namespace water
{
    struct WaterTile;
    struct WaterGlobalSettings;
    struct WaterTileConfig;
}

namespace vegetation
{
    struct WindConfig;
}

namespace render::vegetation
{
    class GrassComputePipeline;
    class GrassMeshShaderPipeline;
    class WindSystem;
}

namespace render::gpudriven
{
    class GPUDrivenRenderer
    {
    private:
        struct TerrainState
        {
            std::unique_ptr<TerrainMeshBuffer> meshBuffer;
            std::unique_ptr<TerrainMeshShaderPipeline> pipeline;
            std::unique_ptr<TerrainGPUAdapter> adapter;
            std::unique_ptr<TerrainStreamManager> streamManager;
            std::vector<TerrainTileGPUData> tileData;
            bool renderingEnabled = true;
            float lodBias = 1.0f;
            float errorThreshold = 2.0f;
            float textureScale = 0.1f;
            uint32_t shadowLOD = 2;
            std::string currentMaterialPath;
            std::vector<TerrainLayerGPUData> layerData;
            bool layerDataDirty = false;
            float updateUs = 0.0f;
            float streamingUs = 0.0f;
            float buildTileDataUs = 0.0f;
            float uploadTileDataUs = 0.0f;
            TerrainStreamManager::TileDataLoader pendingTileDataLoader;
            TerrainStreamManager::TileRAMEvictor pendingTileRAMEvictor;
        };

        struct WaterState
        {
            std::unique_ptr<render::water::WaterPipeline> pipeline;
            std::unique_ptr<render::water::WaterMeshBuffer> meshBuffer;
            std::vector<render::water::WaterTileGPUData> tileData;
            render::water::WaterPushConstants cachedPushConstants{};
            bool renderingEnabled = true;
            int32_t selectedCoordX = 0;
            int32_t selectedCoordZ = 0;
            bool hasSelectedTile = false;

            // Ocean FFT
            std::unique_ptr<render::water::OceanFFT> oceanFFT;
            bool oceanEnabled = false;
        };

        struct VegetationState
        {
            std::unique_ptr<render::vegetation::GrassComputePipeline> grassComputePipeline;
            std::unique_ptr<render::vegetation::GrassMeshShaderPipeline> grassMeshPipeline;
            std::unique_ptr<render::vegetation::WindSystem> windSystem;

            // Buffer manager for vegetation tile allocations
            std::unique_ptr<render::vegetation::VegetationBufferManager> bufferManager;

            // Stream managers
            std::unique_ptr<render::vegetation::GrassStreamManager> grassStreamManager;

            // Grass instance buffer (GPU-side output from compute pipeline)
            vk::Buffer grassInstanceBuffer;
            vk::DeviceMemory grassInstanceBufferMemory;
            vk::Buffer grassCounterBuffer;
            vk::DeviceMemory grassCounterBufferMemory;
            uint32_t grassInstanceCapacity = 0;
            uint32_t currentGrassInstanceCount = 0;

            bool grassRenderingEnabled = true;
            bool grassInitialized = false;

            ::vegetation::GrassRenderConfig grassConfig;

            vk::DescriptorSetLayout cachedIBLLayout;
            vk::RenderPass cachedRenderPass;

            // Staging buffer (host-visible, transfer src) — holds ALL tiles' data at offsets
            vk::Buffer tileStagingBuffer;
            vk::DeviceMemory tileStagingBufferMemory;
            void* tileStagingMapped = nullptr;

            // Device-local compute input buffers (storage + transfer dst) — single tile at a time
            vk::Buffer tileComputeDensity;
            vk::DeviceMemory tileComputeDensityMemory;
            vk::Buffer tileComputeHeight;
            vk::DeviceMemory tileComputeHeightMemory;
            vk::Buffer tileComputeHole;
            vk::DeviceMemory tileComputeHoleMemory;

            uint32_t tileComputeCapacity = 0;   // per-tile texel capacity for compute buffers
            uint32_t tileStagingTileSlots = 0;   // number of tile slots in staging buffer
            uint32_t tileStagingTexelsPerSlot = 0; // texels per slot

            // Track which terrain tiles have vegetation registered
            std::unordered_set<uint64_t> registeredTileKeys;

            // Cached visible tiles for compute dispatch (set during updateVegetationStreaming)
            std::vector<terrain::TerrainTile*> cachedVisibleTiles;

        };

        struct BillboardState
        {
            std::unique_ptr<BillboardBufferManager> bufferManager;
            std::unique_ptr<BillboardMeshShaderPipeline> meshShaderPipeline;
            std::unique_ptr<BillboardStreamManager> streamManager;
            std::vector<BillboardInstanceGPU> instanceList;
            BillboardRenderStats stats;
            bool renderingEnabled = true;
            bool initialized = false;
        };

        struct LightCullingState
        {
            std::unordered_set<uint32_t> visibleLightIds;
            bool useBVH = false;
            bool useOcclusion = false;
            std::unordered_set<uint32_t> prevFrameOccludedLights;
            bool hasPrevFrameOcclusionData = false;
            uint32_t totalSceneLights = 0;
            uint32_t lightsAfterBVHCull = 0;
            uint32_t lightsAfterHiZCull = 0;
        };

        struct MaterialState
        {
            mesh::MaterialTextureCache* textureCache = nullptr;
            std::unordered_set<std::string> registeredPaths;
            std::unordered_map<std::string, std::shared_ptr<material::MaterialData>> loaded;
            std::unordered_map<std::string, mesh::ExtractedPBRValues> pbrCache;
            material::CallbackId changeCallbackId{};
        };

        struct CullingConfig
        {
            bool frustumCullingEnabled = true;
            bool lodSelectionEnabled = true;
            bool occlusionCullingEnabled = true;
            bool distanceCullingEnabled = false;
            float categoryDistances[services::CullingCategory::Count] = {1000.0f, 2000.0f, 500.0f, 300.0f, 200.0f, 500.0f, 1000.0f};
            float shadowDistanceMultiplier = 0.5f;
            float globalLodBias = 0.0f;
            bool meshletFrustumCullingEnabled = true;
            bool meshletBackfaceCullingEnabled = true;
            uint32_t currentViewMode = 0;
        };

        struct CachedCamera
        {
            glm::mat4 view{1.0f};
            glm::mat4 projection{1.0f};
            glm::vec3 position{0.0f};
            float nearPlane = 0.1f;
            float farPlane = 1000.0f;
            float time = 0.0f;
        };

        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<MergedMeshBuffer> mergedBuffer;
        std::unique_ptr<IndirectBatchManager> batchManager;
        std::unique_ptr<BindlessTextureManager> bindlessTextures;
        std::unique_ptr<GPUCullLODPipeline> cullPipeline;
        std::unique_ptr<GPUDrivenCameraBuffer> cameraBuffer;
        std::unique_ptr<MeshShaderPipeline> meshShaderPipeline;
        std::unique_ptr<MeshShaderPipeline> transparentMeshShaderPipeline;
        std::unique_ptr<MeshShaderPipeline> wboitMeshShaderPipeline;
        std::unique_ptr<MeshletBuffer> meshletBuffer;
        std::unique_ptr<BoneMatrixManager> boneMatrixManager;
        std::unique_ptr<lighting::GPULightBufferManager> lightBufferManager;
        std::unique_ptr<lighting::ClusterGridManager> clusterGridManager;
        std::unique_ptr<lighting::LightCullingPipeline> lightCullingPipeline;
        std::unique_ptr<shadow::ShadowSystem> shadowSystem;
        std::unique_ptr<occlusion::LightOcclusionCulling> lightOcclusionCulling;
        std::unique_ptr<volumetric::VolumetricPipeline> volumetricPipeline;
        ::postprocess::VolumetricFogSettings cachedVolumetricSettings;

        // Light streaming
        std::unique_ptr<lighting::LightStreamManager> lightStreamManager;

        // Global Illumination
        std::unique_ptr<gi::RadianceCascadeManager> giCascadeManager;
        std::unique_ptr<gi::ProbeTracePipeline> giTracePipeline;
        std::unique_ptr<gi::ProbeUpdatePipeline> giUpdatePipeline;
        std::unique_ptr<gi::GIDebugRenderer> giDebugRenderer;
        std::unique_ptr<gi::AccelerationStructureManager> accelStructManager;
        gi::GISettings cachedGISettings;
        bool blasNeedsRebuild = true;
        bool giProbeBuffersNeedInit = true;

        std::unique_ptr<mesh::MeshStreamManager> meshStreamManager;
        bool meshStreamingEnabled = true;

        bool initialized = false;
        bool enabled = false;
        bool meshShaderSupported = false;
        uint32_t hiZMipLevels = 0;
        GPUDrivenStats stats{};

        vk::DescriptorSetLayout cachedIBLLayout;
        vk::RenderPass cachedRenderPass;
        vk::RenderPass cachedWBOITRenderPass;

        TerrainState terrain;
        WaterState water;
        VegetationState vegetation;
        BillboardState billboard;
        LightCullingState lightCulling;
        MaterialState materials;
        CullingConfig culling;
        CachedCamera cachedCamera;

    public:
        explicit GPUDrivenRenderer(core::Device& device, core::SwapChain& swapChain);
        ~GPUDrivenRenderer();

        GPUDrivenRenderer(const GPUDrivenRenderer&) = delete;
        GPUDrivenRenderer& operator=(const GPUDrivenRenderer&) = delete;

        void init(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass);

        void cleanup();

        void setDefaultTexture(vk::ImageView view, vk::Sampler sampler);

        void updateScene(
            const std::vector<mesh::MeshRenderData>& opaqueObjects,
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPosition,
            float nearPlane,
            float farPlane,
            float time = 0.0f
        );

        void updateCameraForRTT(const RTTCameraParams& params);

        void restoreMainCamera();

        const glm::mat4& getCachedCameraView() const { return cachedCamera.view; }
        const glm::mat4& getCachedCameraProjection() const { return cachedCamera.projection; }
        const glm::vec3& getCachedCameraPosition() const { return cachedCamera.position; }

        void dispatchCompute(vk::CommandBuffer cmd);

        void renderDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                        uint32_t screenWidth = 0, uint32_t screenHeight = 0);
        void renderTransparentDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                   uint32_t screenWidth = 0, uint32_t screenHeight = 0);
        void renderWBOITDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                             uint32_t screenWidth = 0, uint32_t screenHeight = 0);
        void renderBlendDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                             uint32_t screenWidth = 0, uint32_t screenHeight = 0);

        void renderGIDebug(vk::CommandBuffer cmd, const glm::mat4& viewProjection);

        void initWBOITPipeline(vk::RenderPass wboitRenderPass);
        bool isWBOITReady() const { return wboitMeshShaderPipeline != nullptr && wboitMeshShaderPipeline->getPipeline(); }
        bool hasTransparentObjects() const { return mergedBuffer && mergedBuffer->getTransparentObjectCount() > 0; }

        void setEnabled(bool enabled) { this->enabled = enabled; }
        bool isEnabled() const { return enabled; }

        void setFrustumCullingEnabled(bool enabled) { culling.frustumCullingEnabled = enabled; }
        bool isFrustumCullingEnabled() const { return culling.frustumCullingEnabled; }

        void setLODSelectionEnabled(bool enabled) { culling.lodSelectionEnabled = enabled; }
        bool isLODSelectionEnabled() const { return culling.lodSelectionEnabled; }

        void setOcclusionCullingEnabled(bool enabled) { culling.occlusionCullingEnabled = enabled; }
        bool isOcclusionCullingEnabled() const { return culling.occlusionCullingEnabled; }

        void setMeshletFrustumCullingEnabled(bool enabled) { culling.meshletFrustumCullingEnabled = enabled; }
        bool isMeshletFrustumCullingEnabled() const { return culling.meshletFrustumCullingEnabled; }
        void setMeshletBackfaceCullingEnabled(bool enabled) { culling.meshletBackfaceCullingEnabled = enabled; }
        bool isMeshletBackfaceCullingEnabled() const { return culling.meshletBackfaceCullingEnabled; }

        void setDistanceCullingEnabled(bool enabled) { culling.distanceCullingEnabled = enabled; }
        bool isDistanceCullingEnabled() const { return culling.distanceCullingEnabled; }
        void setCategoryDistance(uint32_t category, float distance) { if (category < services::CullingCategory::Count) culling.categoryDistances[category] = distance; }
        void setShadowDistanceMultiplier(float mult) { culling.shadowDistanceMultiplier = mult; }
        void setGlobalLodBias(float bias) { culling.globalLodBias = bias; }
        float getGlobalLodBias() const { return culling.globalLodBias; }

        void setTerrainFrustumCullingEnabled(bool enabled);
        void setTerrainMeshletCullingEnabled(bool enabled);

        void setViewMode(uint32_t mode) { culling.currentViewMode = mode; }
        uint32_t getViewMode() const { return culling.currentViewMode; }

        void updateHiZPyramid(vk::ImageView hiZView, vk::Sampler hiZSampler, uint32_t mipLevels);

        const GPUDrivenStats& getStats() const { return stats; }

        void updateStatsFromGPU();

        MeshletCullingStats getMeshletCullingStats();

        void setMaterialTextureCache(mesh::MaterialTextureCache* cache) { materials.textureCache = cache; }

        uint32_t getHiZMipLevels() const { return hiZMipLevels; }

        void updateRenderPass(vk::RenderPass newRenderPass, vk::DescriptorSetLayout newIBLLayout = nullptr);

        uint32_t getMergedVertexCount() const;
        uint32_t getMergedIndexCount() const;
        uint32_t getRegisteredMeshCount() const;
        uint32_t getRegisteredTextureCount() const;

        uint32_t getBatchCount() const;
        uint32_t getCommandsPerBatch() const;
        uint32_t getTotalCapacity() const;
        uint64_t getDrawCommandBufferSize() const;
        uint64_t getDrawCountBufferSize() const;
        uint64_t getPerDrawDataBufferSize() const;
        uint64_t getTotalMemoryUsage() const;

        lighting::GPULightBufferManager* getLightBufferManager() const { return lightBufferManager.get(); }
        lighting::ClusterGridManager* getClusterGridManager() const { return clusterGridManager.get(); }
        shadow::ShadowSystem* getShadowSystem() const { return shadowSystem.get(); }

        void setDeletionQueue(core::DeferredDeletionQueue* queue);

        // Light streaming
        lighting::LightStreamManager* getLightStreamManager() const { return lightStreamManager.get(); }
        void initLightStreaming(const lighting::LightStreamingConfig& config = {});

        // Global Illumination
        void initGI(const gi::GISettings& settings);
        void cleanupGI();
        void applyGISettings(const gi::GISettings& settings);
        gi::RadianceCascadeManager* getGICascadeManager() const { return giCascadeManager.get(); }
        gi::GIDebugRenderer* getGIDebugRenderer() const { return giDebugRenderer.get(); }
        const gi::GISettings& getGISettings() const { return cachedGISettings; }

        void setVisibleLightsFromBVH(const std::vector<uint32_t>& visibleLights);
        void clearVisibleLights();
        bool isBVHLightCullingEnabled() const { return lightCulling.useBVH; }

        void initLightOcclusionCulling(occlusion::HiZBuffer* hiZBuffer);
        bool isLightOcclusionCullingEnabled() const { return lightCulling.useOcclusion; }

        uint32_t getTotalSceneLights() const;
        uint32_t getLightsAfterBVHCull() const;
        uint32_t getLightsAfterHiZCull() const;

        void readBackLightOcclusionResults();

        void initVolumetricFog(::postprocess::VolumetricQuality quality);
        void setVolumetricFogEnabled(bool enabled);
        bool isVolumetricFogEnabled() const;
        volumetric::VolumetricPipeline* getVolumetricPipeline() const { return volumetricPipeline.get(); }
        void updateVolumetricSettings(const ::postprocess::VolumetricFogSettings& settings);

        void updateTerrain(const std::vector<terrain::TerrainTile*>& visibleTiles,
                           const glm::vec3& cameraPosition,
                           const std::string& terrainMaterialPath = "");
        void renderTerrainDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                               uint32_t screenWidth = 0, uint32_t screenHeight = 0);
        void clearTerrainData();
        void evictTerrainTile(int32_t coordX, int32_t coordZ);
        void setSelectedTerrainTile(int32_t coordX, int32_t coordZ);
        void clearSelectedTerrainTile();

        void invalidateTerrainLayerData() { terrain.layerDataDirty = true; }

        void setTerrainRenderingEnabled(bool enabled) { terrain.renderingEnabled = enabled; }
        bool isTerrainRenderingEnabled() const { return terrain.renderingEnabled; }
        void setTerrainLODBias(float bias) { terrain.lodBias = bias; }
        void setTerrainErrorThreshold(float threshold) { terrain.errorThreshold = threshold; }
        void setTerrainTextureScale(float scale) { terrain.textureScale = scale; }
        void setTerrainShadowLOD(uint32_t lod) { terrain.shadowLOD = std::min(lod, 3u); }

        void updateWater(const std::vector<::water::WaterTile*>& visibleTiles,
                         const ::water::WaterGlobalSettings& settings,
                         const ::water::WaterTileConfig& tileConfig);
        void renderWaterDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet);
        void clearWaterData();
        void setSelectedWaterTile(int32_t coordX, int32_t coordZ);
        void clearSelectedWaterTile();

        void setWaterRenderingEnabled(bool enabled) { water.renderingEnabled = enabled; }
        bool isWaterRenderingEnabled() const { return water.renderingEnabled; }

        void initOceanFFT(const render::water::OceanFFTConfig& config);
        void cleanupOceanFFT();
        void setOceanEnabled(bool enabled);
        bool isOceanEnabled() const { return water.oceanEnabled; }
        void updateOceanConfig(const render::water::OceanFFTConfig& config);
        void dispatchOceanFFT(vk::CommandBuffer cmd, float time);
        void readbackOceanDisplacement();
        float getOceanHeightAt(const glm::vec2& worldXZ) const;

        // Vegetation rendering
        void initVegetationSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass);
        void renderGrassDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                             uint32_t screenWidth = 0, uint32_t screenHeight = 0);
        void updateWind(float deltaTime, const ::vegetation::WindConfig& config);
        void updateVegetationStreaming(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                       const std::vector<terrain::TerrainTile*>& allLoadedTiles,
                                       const glm::vec3& cameraPosition);
        void setGrassRenderingEnabled(bool enabled) { vegetation.grassRenderingEnabled = enabled; }
        bool isGrassRenderingEnabled() const { return vegetation.grassRenderingEnabled; }
        void setGrassRenderConfig(const ::vegetation::GrassRenderConfig& config) { vegetation.grassConfig = config; }
        void addVegetationTile(int32_t coordX, int32_t coordZ);
        void removeVegetationTile(int32_t coordX, int32_t coordZ);
        void clearVegetationData();
        void markVegetationTileDirty(int32_t coordX, int32_t coordZ);
        void ensureTileStagingBuffers(uint32_t texelsPerTile, uint32_t tileCount);
        void dispatchGrassCompute(vk::CommandBuffer cmd, const std::vector<terrain::TerrainTile*>& visibleTiles);
        void cleanupVegetation();

        // Billboard rendering
        void updateBillboards(const std::vector<BillboardInstanceGPU>& instances);
        void renderBillboardDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                  uint32_t screenWidth = 0, uint32_t screenHeight = 0);
        void clearBillboardData();
        void setBillboardRenderingEnabled(bool enabled) { billboard.renderingEnabled = enabled; }
        bool isBillboardRenderingEnabled() const { return billboard.renderingEnabled; }
        const BillboardRenderStats& getBillboardStats() const { return billboard.stats; }

        void setBrushOverlay(const glm::vec2& worldPos, float worldRadius, float falloff, float shape);

        void setTileDataLoader(TerrainStreamManager::TileDataLoader loader);
        void releaseMeshAsset(const std::string& meshPath);
        void releaseTextureAsset(const std::string& texturePath);
        void releaseMaterialAsset(const std::string& materialPath);

        void setTileRAMEvictor(TerrainStreamManager::TileRAMEvictor evictor);

        float getTerrainUpdateUs() const { return terrain.updateUs; }
        float getTerrainStreamingUs() const { return terrain.streamingUs; }
        float getTerrainBuildTileDataUs() const { return terrain.buildTileDataUs; }
        float getTerrainUploadTileDataUs() const { return terrain.uploadTileDataUs; }
        const TerrainStreamingStats* getTerrainStreamingStats() const;
        TerrainCullingStats getTerrainCullingStats();

    private:
        bool registerMaterialTextures(const std::string& materialPath);
        void registerTerrainLayerTextures(const std::string& materialPath);
        void registerTextureDependencies(const std::string& materialPath,
                                          const std::vector<std::string>& texturePaths);

        void updateMeshStreaming(const std::vector<mesh::MeshRenderData>& opaqueObjects,
                                 const glm::vec3& cameraPosition);
        void registerSceneMaterialTextures(const std::vector<mesh::MeshRenderData>& opaqueObjects);
        TextureIndexResolver createTextureResolver();
        BoneOffsetResolver updateAnimationBones();
        void updateClusterGrid(const glm::mat4& projection, float nearPlane, float farPlane);
        void updatePipelineDescriptors();

        void initBillboardSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass);
        void initTerrainSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass);
        void createGrassBuffers(uint32_t maxInstances);
        void initWaterSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass);
        void collectShadowVisibleLights(std::unordered_set<uint32_t>& outLights, bool& outHasFilter);
        void buildAndDispatchLightOcclusion(vk::CommandBuffer cmd);
        void recordShadowPasses(vk::CommandBuffer cmd, bool hasMeshObjects, bool hasTerrainTiles);
        void updateLightCullingState(vk::CommandBuffer cmd);
        void dispatchVolumetricFog(vk::CommandBuffer cmd);
        void dispatchGIProbeUpdate(vk::CommandBuffer cmd);

        static uint64_t makeTileKey(int32_t x, int32_t z);
    };
}
