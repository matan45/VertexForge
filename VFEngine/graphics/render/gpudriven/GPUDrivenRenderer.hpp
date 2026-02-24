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
#include "MeshletBuffer.hpp"
#include "BoneMatrixManager.hpp"
#include "../lighting/GPULightBufferManager.hpp"
#include "../lighting/ClusterGridManager.hpp"
#include "../lighting/LightCullingPipeline.hpp"
#include "../shadow/ShadowSystem.hpp"
#include "../occlusion/LightOcclusionCulling.hpp"
#include "../volumetric/VolumetricPipeline.hpp"
#include "../material/MaterialPBRExtractor.hpp"
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

namespace render::gpudriven
{
    class GPUDrivenRenderer
    {
    private:
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

        std::unique_ptr<TerrainMeshBuffer> terrainMeshBuffer;
        std::unique_ptr<TerrainMeshShaderPipeline> terrainPipeline;
        std::unique_ptr<TerrainGPUAdapter> terrainAdapter;
        std::unique_ptr<TerrainStreamManager> terrainStreamManager;
        std::vector<TerrainTileGPUData> terrainTileData;
        bool terrainRenderingEnabled = true;
        float terrainLODBias = 1.0f;
        float terrainErrorThreshold = 2.0f;
        float terrainTextureScale = 0.1f;
        uint32_t terrainShadowLOD = 2;  // LOD level for terrain shadow rendering (0=highest detail, 3=lowest)

        // Water subsystem
        std::unique_ptr<render::water::WaterPipeline> waterPipeline;
        std::unique_ptr<render::water::WaterMeshBuffer> waterMeshBuffer;
        std::vector<render::water::WaterTileGPUData> waterTileData;
        render::water::WaterPushConstants cachedWaterPushConstants{};
        bool waterRenderingEnabled = true;

        bool initialized = false;
        bool enabled = false;
        bool frustumCullingEnabled = true;
        bool lodSelectionEnabled = true;
        bool occlusionCullingEnabled = true;

        bool meshletFrustumCullingEnabled = true;
        bool meshletBackfaceCullingEnabled = true;
        bool meshShaderSupported = false;
        uint32_t currentViewMode = 0;

        uint32_t hiZMipLevels = 0;

        GPUDrivenStats stats{};

        vk::DescriptorSetLayout cachedIBLLayout;
        vk::RenderPass cachedRenderPass;
        vk::RenderPass cachedWBOITRenderPass;

        mesh::MaterialTextureCache* materialTextureCache = nullptr;

        std::unordered_set<std::string> registeredMaterialPaths;

        std::unordered_map<std::string, std::shared_ptr<material::MaterialData>> loadedMaterials;

        std::unordered_map<std::string, mesh::ExtractedPBRValues> pbrCache;
        material::CallbackId materialChangeCallbackId{};

        std::unique_ptr<mesh::MeshStreamManager> meshStreamManager;
        bool meshStreamingEnabled = true;

        std::unordered_set<uint32_t> visibleLightIds;
        bool useBVHLightCulling = false;
        bool useLightOcclusionCulling = false;

        std::unordered_set<uint32_t> prevFrameOccludedLights;
        bool hasPrevFrameOcclusionData = false;

        uint32_t totalSceneLights = 0;
        uint32_t lightsAfterBVHCull = 0;
        uint32_t lightsAfterHiZCull = 0;

        glm::mat4 cachedCameraView{1.0f};
        glm::mat4 cachedCameraProjection{1.0f};
        float cachedCameraNear = 0.1f;
        float cachedCameraFar = 1000.0f;

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

        void dispatchCompute(vk::CommandBuffer cmd);

        void renderDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet);
        void renderTransparentDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet);
        void renderWBOITDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet);
        void renderBlendDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet);

        void initWBOITPipeline(vk::RenderPass wboitRenderPass);
        bool isWBOITReady() const { return wboitMeshShaderPipeline != nullptr && wboitMeshShaderPipeline->getPipeline(); }
        bool hasTransparentObjects() const { return mergedBuffer && mergedBuffer->getTransparentObjectCount() > 0; }

        void setEnabled(bool enabled) { this->enabled = enabled; }
        bool isEnabled() const { return enabled; }

        void setFrustumCullingEnabled(bool enabled) { frustumCullingEnabled = enabled; }
        bool isFrustumCullingEnabled() const { return frustumCullingEnabled; }

        void setLODSelectionEnabled(bool enabled) { lodSelectionEnabled = enabled; }
        bool isLODSelectionEnabled() const { return lodSelectionEnabled; }

        void setOcclusionCullingEnabled(bool enabled) { occlusionCullingEnabled = enabled; }
        bool isOcclusionCullingEnabled() const { return occlusionCullingEnabled; }

        void setMeshletFrustumCullingEnabled(bool enabled) { meshletFrustumCullingEnabled = enabled; }
        bool isMeshletFrustumCullingEnabled() const { return meshletFrustumCullingEnabled; }
        void setMeshletBackfaceCullingEnabled(bool enabled) { meshletBackfaceCullingEnabled = enabled; }
        bool isMeshletBackfaceCullingEnabled() const { return meshletBackfaceCullingEnabled; }

        void setTerrainFrustumCullingEnabled(bool enabled);
        void setTerrainMeshletCullingEnabled(bool enabled);

        void setViewMode(uint32_t mode) { currentViewMode = mode; }
        uint32_t getViewMode() const { return currentViewMode; }

        void updateHiZPyramid(vk::ImageView hiZView, vk::Sampler hiZSampler, uint32_t mipLevels);

        const GPUDrivenStats& getStats() const { return stats; }

        void updateStatsFromGPU();

        MeshletCullingStats getMeshletCullingStats();

        void setMaterialTextureCache(mesh::MaterialTextureCache* cache) { materialTextureCache = cache; }

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
        bool isBVHLightCullingEnabled() const { return useBVHLightCulling; }

        void initLightOcclusionCulling(occlusion::HiZBuffer* hiZBuffer);
        bool isLightOcclusionCullingEnabled() const { return useLightOcclusionCulling; }

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
        void renderTerrainDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet);
        void clearTerrainData();

        void setTerrainRenderingEnabled(bool enabled) { terrainRenderingEnabled = enabled; }
        bool isTerrainRenderingEnabled() const { return terrainRenderingEnabled; }
        void setTerrainLODBias(float bias) { terrainLODBias = bias; }
        void setTerrainErrorThreshold(float threshold) { terrainErrorThreshold = threshold; }
        void setTerrainTextureScale(float scale) { terrainTextureScale = scale; }
        void setTerrainShadowLOD(uint32_t lod) { terrainShadowLOD = std::min(lod, 3u); }

        // Water rendering
        void updateWater(const std::vector<::water::WaterTile*>& visibleTiles,
                         const ::water::WaterGlobalSettings& settings,
                         const ::water::WaterTileConfig& tileConfig);
        void renderWaterDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet);
        void clearWaterData();

        void setWaterRenderingEnabled(bool enabled) { waterRenderingEnabled = enabled; }
        bool isWaterRenderingEnabled() const { return waterRenderingEnabled; }

        void setBrushOverlay(const glm::vec2& worldPos, float worldRadius, float falloff, float shape);

        void setTileDataLoader(TerrainStreamManager::TileDataLoader loader);
        void setTileRAMEvictor(TerrainStreamManager::TileRAMEvictor evictor);

        float getTerrainUpdateUs() const { return terrainUpdateUs_; }
        float getTerrainStreamingUs() const { return terrainStreamingUs_; }
        float getTerrainBuildTileDataUs() const { return terrainBuildTileDataUs_; }
        float getTerrainUploadTileDataUs() const { return terrainUploadTileDataUs_; }
        const TerrainStreamingStats* getTerrainStreamingStats() const;
        TerrainCullingStats getTerrainCullingStats();

    private:
        std::string currentTerrainMaterialPath_;
        std::vector<TerrainLayerGPUData> terrainLayerData_;

        float terrainUpdateUs_ = 0.0f;
        float terrainStreamingUs_ = 0.0f;
        float terrainBuildTileDataUs_ = 0.0f;
        float terrainUploadTileDataUs_ = 0.0f;

        // Pending callbacks (stored until terrainStreamManager is created)
        TerrainStreamManager::TileDataLoader pendingTileDataLoader_;
        TerrainStreamManager::TileRAMEvictor pendingTileRAMEvictor_;

        bool registerMaterialTextures(const std::string& materialPath);
        void registerTerrainLayerTextures(const std::string& materialPath);

        void updateMeshStreaming(const std::vector<mesh::MeshRenderData>& opaqueObjects,
                                 const glm::vec3& cameraPosition);
        void registerSceneMaterialTextures(const std::vector<mesh::MeshRenderData>& opaqueObjects);
        TextureIndexResolver createTextureResolver();
        BoneOffsetResolver updateAnimationBones();
        void updateClusterGrid(const glm::mat4& projection, float nearPlane, float farPlane);
        void updatePipelineDescriptors();

        void initTerrainSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass);
        void initWaterSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass);
        void collectShadowVisibleLights(std::unordered_set<uint32_t>& outLights, bool& outHasFilter);
        void buildAndDispatchLightOcclusion(vk::CommandBuffer cmd);
        void recordShadowPasses(vk::CommandBuffer cmd, bool hasMeshObjects, bool hasTerrainTiles);
    };
}
