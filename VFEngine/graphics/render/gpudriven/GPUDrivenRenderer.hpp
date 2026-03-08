#pragma once

#include "GPUDrivenTypes.hpp"
#include "MergedMeshBuffer.hpp"
#include "IndirectBatchManager.hpp"
#include "BindlessTextureManager.hpp"
#include "GPUCullLODPipeline.hpp"
#include "GPUDrivenCameraBuffer.hpp"
#include "MeshShaderPipeline.hpp"
#include "TerrainMeshShaderPipeline.hpp"
#include "TerrainMeshBuffer.hpp"
#include "TerrainGPUAdapter.hpp"
#include "TerrainStreamManager.hpp"
#include "../water/WaterPipeline.hpp"
#include "../water/WaterMeshBuffer.hpp"
#include "../water/WaterGPUTypes.hpp"
#include "../water/OceanFFT.hpp"
#include "MeshletBuffer.hpp"
#include "BoneMatrixManager.hpp"
#include "../lighting/GPULightBufferManager.hpp"
#include "../lighting/ClusterGridManager.hpp"
#include "../lighting/LightCullingPipeline.hpp"
#include "../shadow/ShadowSystem.hpp"
#include "../vegetation/WindSystem.hpp"
#include "../vegetation/VegetationBufferManager.hpp"
#include "../vegetation/GrassStreamManager.hpp"
#include "../vegetation/VegetationStreamManager.hpp"
#include "../occlusion/LightOcclusionCulling.hpp"
#include "../volumetric/VolumetricPipeline.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "../../core/Texture.hpp"
#include "material/MaterialManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
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
            std::unique_ptr<render::vegetation::VegetationStreamManager> vegetationStreamManager;

            // Grass instance buffer (GPU-side output from compute pipeline)
            vk::Buffer grassInstanceBuffer;
            vk::DeviceMemory grassInstanceBufferMemory;
            vk::Buffer grassCounterBuffer;
            vk::DeviceMemory grassCounterBufferMemory;
            void* grassCounterMapped = nullptr;
            uint32_t grassInstanceCapacity = 0;
            uint32_t currentGrassInstanceCount = 0;

            bool grassRenderingEnabled = true;
            bool vegetationRenderingEnabled = true;
            bool grassInitialized = false;

            float grassFadeStart = 100.0f;
            float grassFadeEnd = 150.0f;

            vk::DescriptorSetLayout cachedIBLLayout;
            vk::RenderPass cachedRenderPass;

            // Track which terrain tiles have vegetation registered
            std::unordered_set<uint64_t> registeredTileKeys;
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
            std::unordered_map<std::string, std::unique_ptr<core::Texture>> lightmapTextureCache;
            std::unordered_set<std::string> registeredLightmapPaths;
        };

        struct CullingConfig
        {
            bool frustumCullingEnabled = true;
            bool lodSelectionEnabled = true;
            bool occlusionCullingEnabled = true;
            bool distanceCullingEnabled = false;
            float categoryDistances[5] = {1000.0f, 2000.0f, 500.0f, 300.0f, 200.0f};
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
        void setCategoryDistance(uint32_t category, float distance) { if (category < 5) culling.categoryDistances[category] = distance; }
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

        struct TerrainTileLightmapData
        {
            int32_t coordX = 0;
            int32_t coordZ = 0;
            glm::vec4 scaleOffset{1.0f, 1.0f, 0.0f, 0.0f};
            std::string lightmapPath;
        };
        void setTerrainLightmapData(const std::vector<TerrainTileLightmapData>& data);
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
                                       const glm::vec3& cameraPosition);
        void setGrassRenderingEnabled(bool enabled) { vegetation.grassRenderingEnabled = enabled; }
        bool isGrassRenderingEnabled() const { return vegetation.grassRenderingEnabled; }
        void setGrassFadeDistances(float start, float end) { vegetation.grassFadeStart = start; vegetation.grassFadeEnd = end; }
        void addVegetationTile(int32_t coordX, int32_t coordZ);
        void removeVegetationTile(int32_t coordX, int32_t coordZ);
        void markVegetationTileDirty(int32_t coordX, int32_t coordZ);
        void cleanupVegetation();

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

        void updateMeshStreaming(const std::vector<mesh::MeshRenderData>& opaqueObjects,
                                 const glm::vec3& cameraPosition);
        void registerSceneMaterialTextures(const std::vector<mesh::MeshRenderData>& opaqueObjects);
        void registerSceneLightmapTextures(const std::vector<mesh::MeshRenderData>& opaqueObjects);
        LightmapIndexResolver createLightmapResolver();
        TextureIndexResolver createTextureResolver();
        BoneOffsetResolver updateAnimationBones();
        void updateClusterGrid(const glm::mat4& projection, float nearPlane, float farPlane);
        void updatePipelineDescriptors();

        void initTerrainSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass);
        void createGrassBuffers(uint32_t maxInstances);
        void initWaterSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass);
        void collectShadowVisibleLights(std::unordered_set<uint32_t>& outLights, bool& outHasFilter);
        void buildAndDispatchLightOcclusion(vk::CommandBuffer cmd);
        void recordShadowPasses(vk::CommandBuffer cmd, bool hasMeshObjects, bool hasTerrainTiles);
    };
}
