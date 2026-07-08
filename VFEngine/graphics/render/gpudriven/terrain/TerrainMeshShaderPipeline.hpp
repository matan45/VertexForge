#pragma once

#include "../../../core/VulkanMemoryManager.hpp"
#include "../../raytracing/RTShadowMaskSet.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <array>
#include "../GPUDrivenTypes.hpp"

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::gpudriven
{
    class TerrainMeshBuffer;

    struct TerrainPushConstants
    {
        uint32_t tileCount;
        uint32_t viewMode;
        float screenWidth;
        float screenHeight;
        float lodBias;           // LOD quality bias (1.0 = normal)
        float errorThreshold;    // Screen-space error threshold in pixels
        float terrainTextureScale; // Scale for world-space UV tiling
        float terrainMaxDrawDistSq; // Squared max draw distance for terrain (0 = disabled)
        glm::vec2 brushWorldPos;
        float brushWorldRadius;    // 0.0 = inactive
        float brushFalloff;        // Falloff type (0=constant, 1=linear, 2=smooth, 3=sharp)
        float brushShape;          // Shape (0=circle, 1=square)
        float brushWorldY;         // Brush overlay Y position for cave awareness
        uint32_t hiZMipLevels;     // Mip levels in the Hi-Z pyramid (0 = disabled)
        float _pad3;               // Align mat4 to 16-byte boundary (offset 64)
        glm::mat4 viewProjection; // CPU-precomputed view-projection (matches raycast invViewProjection)
        // Stamp overlay (after mat4, offset 128)
        uint32_t stampWidth;
        uint32_t stampHeight;
        float stampRotation;     // radians
        float _padStamp;
    };

    // Terrain culling bits (same as regular mesh shader bits)
    constexpr uint32_t TERRAIN_CULL_FRUSTUM_BIT = 0x100;
    constexpr uint32_t TERRAIN_CULL_BACKFACE_BIT = 0x200;
    constexpr uint32_t TERRAIN_CULL_OCCLUSION_BIT = 0x800;

    class TerrainMeshShaderPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<core::Shader> terrainShader;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout terrainDataLayout;
        vk::DescriptorPool terrainDataPool;
        vk::DescriptorSet terrainDataDescriptorSet;

        vk::Buffer tileDataBuffer;
        core::VulkanAllocation tileDataBufferAllocation;
        uint32_t maxTileCount = 4096;
        uint32_t currentTileCount = 0;

        vk::Buffer statsBuffer;
        core::VulkanAllocation statsBufferAllocation;
        TerrainCullingStats cachedStats{};

        void* tileDataBufferMapped = nullptr;

        vk::DescriptorSetLayout cachedIBLLayout;
        vk::DescriptorSetLayout cachedBindlessLayout;
        vk::DescriptorSetLayout cachedMeshletLayout;
        vk::DescriptorSetLayout cachedVertexLayout;
        vk::DescriptorSetLayout cachedLightDataLayout;
        vk::DescriptorSetLayout cachedClusterGridLayout;
        vk::DescriptorSetLayout cachedCullingOutputLayout;
        vk::DescriptorSetLayout cachedShadowDataLayout;
        vk::DescriptorSetLayout cachedShadowTextureLayout;

        vk::DescriptorSetLayout emptyLayout;
        vk::DescriptorPool emptyDescriptorPool;
        vk::DescriptorSet emptyDescriptorSet5;

        // VK-1209 terrain RVT sample resources (set 5, replacing the empty placeholder when
        // RVT is active). Layout/set/params-UBO always exist (cheap); the pipeline only uses
        // them + compiles the RVT_ENABLED shader path when rvtSampleEnabled is set, so terrain
        // stays byte-identical with RVT off.
        bool rvtSampleEnabled = false;
        vk::DescriptorSetLayout rvtSampleLayout;
        vk::DescriptorPool rvtSamplePool;
        vk::DescriptorSet rvtSampleDescriptorSet;
        vk::Buffer rvtParamsBuffer;
        core::VulkanAllocation rvtParamsAllocation;

        // Caustic descriptor set (Set 12) - owned by WaterCausticsResources
        vk::DescriptorSetLayout cachedCausticLayout;
        vk::DescriptorSet causticDescriptorSet;
        bool causticEnabled = false;

        // RT shadow mask descriptor set (Set 12, when caustics not active)
        vk::DescriptorSetLayout rtShadowMaskLayout;
        vk::DescriptorSet rtShadowMaskDescriptorSet;
        bool rtShadowEnabled = false;
        bool pipelineHasSet12 = false;  // True when the active pipeline layout includes set 12

        // Per-spot-light RT shadow mask array (VK-1175). The producer descriptor set is copied into
        // the shared RT mask set (set 13, binding 1) — see rtMaskSet below.
        vk::DescriptorSetLayout rtSpotShadowMaskLayout;
        vk::DescriptorSet rtSpotShadowMaskDescriptorSet;
        bool rtSpotShadowEnabled = false;

        // Per-point-light RT shadow mask array (VK-1176). Copied into the shared set (binding 2).
        vk::DescriptorSetLayout rtPointShadowMaskLayout;
        vk::DescriptorSet rtPointShadowMaskDescriptorSet;
        bool rtPointShadowEnabled = false;

        // Shared RT shadow mask set bound at set 13 (directional binding 0, spot 1, point 2). The
        // three producer descriptor sets above are copied into it, freeing sets 15/16 (<=14 sets).
        std::unique_ptr<raytracing::RTShadowMaskSet> rtMaskSet;
        bool rtMaskBound = false;
        // VK-1398: per-image-slot dirty flags. updateRT*ShadowMaskDescriptor caches the producer and
        // marks slots dirty; ensureRTMaskSlot() copies into the slot for the image being recorded.
        std::array<bool, core::MAX_SWAPCHAIN_IMAGES> rtMaskSlotDirty{};
        void markAllRTMaskSlotsDirty() { rtMaskSlotDirty.fill(true); }

        // Plugin world-space mask (Set 11 bindings 3/4 — sampler + params UBO).
        // The bindings always exist in terrainDataLayout; the WORLD_MASK_ENABLED macro
        // (and thus the shader cost) is only compiled in once a mask is bound.
        bool worldMaskEnabled = false;

        // Weight map + layer info descriptor (Set 1)
        vk::DescriptorSetLayout weightMapLayout;
        vk::DescriptorPool weightMapPool;
        vk::DescriptorSet weightMapDescriptorSet;

        // Terrain layer info buffer (Set 1, binding 1) - host-visible for easy updates
        vk::Buffer terrainLayerBuffer;
        core::VulkanAllocation terrainLayerBufferAllocation;
        void* terrainLayerBufferMapped = nullptr;

        // Shared descriptor sets (owned elsewhere)
        vk::DescriptorSet iblDescriptorSet;
        vk::DescriptorSet bindlessDescriptorSet;
        vk::DescriptorSet lightDataDescriptorSet;
        vk::DescriptorSet clusterGridDescriptorSet;
        vk::DescriptorSet cullingOutputDescriptorSet;
        vk::DescriptorSet shadowDataDescriptorSet;
        vk::DescriptorSet shadowTextureDescriptorSet;

        // Terrain-specific descriptor sets (owned by this pipeline)
        vk::DescriptorPool terrainBufferPool;
        vk::DescriptorSet terrainMeshletDescriptorSet;
        vk::DescriptorSet terrainVertexDescriptorSet;

        bool initialized = false;
        bool wireframeMode = false;

        glm::vec2 brushWorldPos{0.0f};
        float brushWorldY = 0.0f;
        float brushWorldRadius = 0.0f;
        float brushFalloff = 0.0f;
        float brushShape = 0.0f;
        float terrainMaxDrawDistSq = 0.0f;
        glm::mat4 viewProjection{1.0f};

        // Stamp overlay
        vk::Buffer stampDummyBuffer;
        core::VulkanAllocation stampDummyAllocation;
        vk::Buffer stampOverlayBuffer;
        uint32_t stampOverlayWidth = 0;
        uint32_t stampOverlayHeight = 0;
        float stampOverlayRotation = 0.0f;
        bool stampOverlayDirty = false;

    public:
        explicit TerrainMeshShaderPipeline(core::Device& device, core::SwapChain& swapChain);
        ~TerrainMeshShaderPipeline();

        TerrainMeshShaderPipeline(const TerrainMeshShaderPipeline&) = delete;
        TerrainMeshShaderPipeline& operator=(const TerrainMeshShaderPipeline&) = delete;

        void init(vk::DescriptorSetLayout iblLayout,
                  vk::DescriptorSetLayout bindlessTextureLayout,
                  vk::DescriptorSetLayout meshletDataLayout,
                  vk::DescriptorSetLayout vertexDataLayout,
                  vk::DescriptorSetLayout lightDataLayout,
                  vk::DescriptorSetLayout clusterGridLayout,
                  vk::DescriptorSetLayout cullingOutputLayout,
                  vk::DescriptorSetLayout shadowDataLayout,
                  vk::DescriptorSetLayout shadowTextureLayout,
                  const std::vector<vk::Format>& colorFormats, vk::Format depthFormat);

        void cleanup();
        void recreate(vk::DescriptorSetLayout iblLayout,
                      vk::DescriptorSetLayout bindlessTextureLayout,
                      vk::DescriptorSetLayout meshletDataLayout,
                      vk::DescriptorSetLayout vertexDataLayout,
                      vk::DescriptorSetLayout lightDataLayout,
                      vk::DescriptorSetLayout clusterGridLayout,
                      vk::DescriptorSetLayout cullingOutputLayout,
                      vk::DescriptorSetLayout shadowDataLayout,
                      vk::DescriptorSetLayout shadowTextureLayout,
                      const std::vector<vk::Format>& colorFormats, vk::Format depthFormat);

        void setWireframeMode(bool enabled) { wireframeMode = enabled; }

        void updateTileData(const std::vector<TerrainTileGPUData>& tiles);

        void updateTerrainBufferDescriptors(TerrainMeshBuffer& terrainBuffer);
        void updateHiZDescriptor(vk::ImageView hiZView, vk::Sampler hiZSampler);

        void updateWeightMapDescriptor(vk::Buffer weightMapBuffer);

        void updateTerrainLayerInfo(const std::vector<TerrainLayerGPUData>& layers);

        void updateSharedDescriptors(vk::DescriptorSet iblDescSet,
                                     vk::DescriptorSet bindlessDescSet,
                                     vk::DescriptorSet lightDataDescSet,
                                     vk::DescriptorSet clusterGridDescSet,
                                     vk::DescriptorSet cullingOutputDescSet,
                                     vk::DescriptorSet shadowDataDescSet,
                                     vk::DescriptorSet shadowTextureDescSet);

        void setRTShadowMaskLayout(vk::DescriptorSetLayout layout) { rtShadowMaskLayout = layout; rtShadowEnabled = true; }
        // VK-1398: cache producers only; copyInto the ringed set happens lazily in ensureRTMaskSlot().
        void updateRTShadowMaskDescriptor(vk::DescriptorSet descSet)
        {
            if (rtShadowMaskDescriptorSet != descSet) { rtShadowMaskDescriptorSet = descSet; markAllRTMaskSlotsDirty(); }
        }

        void setRTSpotShadowMaskLayout(vk::DescriptorSetLayout layout) { rtSpotShadowMaskLayout = layout; rtSpotShadowEnabled = true; }
        void updateRTSpotShadowMaskDescriptor(vk::DescriptorSet descSet)
        {
            if (rtSpotShadowMaskDescriptorSet != descSet) { rtSpotShadowMaskDescriptorSet = descSet; markAllRTMaskSlotsDirty(); }
        }
        void setRTPointShadowMaskLayout(vk::DescriptorSetLayout layout) { rtPointShadowMaskLayout = layout; rtPointShadowEnabled = true; }
        void updateRTPointShadowMaskDescriptor(vk::DescriptorSet descSet)
        {
            if (rtPointShadowMaskDescriptorSet != descSet) { rtPointShadowMaskDescriptorSet = descSet; markAllRTMaskSlotsDirty(); }
        }

        // Lazily populates the ring slot for imageIndex from the cached producers (only when dirty).
        void ensureRTMaskSlot(uint32_t imageIndex);

        // Plugin world mask: enables the WORLD_MASK_ENABLED macro on the next (re)create
        // and writes the sampler + params UBO into set 11 bindings 3/4.
        void setWorldMaskEnabled(bool enabled) { worldMaskEnabled = enabled; }
        bool isWorldMaskEnabled() const { return worldMaskEnabled; }

        // VK-1209: enable the RVT sample path (set 5 + RVT_ENABLED macro). Takes effect on the
        // next (re)create. updateRVTSampleResources writes the page table / atlases / feedback /
        // params UBO into the set-5 descriptor (params = 64-byte RVTParams blob built by the caller).
        // Finding #15: the set-5 descriptor is allocated lazily on the first enable after init (runtime
        // toggle goes through recreate(), not init()), so a build that never enables RVT never allocates it.
        void setRVTSampleEnabled(bool enabled)
        {
            rvtSampleEnabled = enabled;
            if (enabled && initialized && !rvtSampleDescriptorSet)
                createRVTSampleDescriptor();
        }
        bool isRVTSampleEnabled() const { return rvtSampleEnabled; }
        void updateRVTSampleResources(vk::Buffer pageTableBuffer, vk::ImageView albedoView,
                                      vk::ImageView ormView, vk::Sampler sampler,
                                      vk::Buffer feedbackBuffer, const void* params, vk::DeviceSize paramsSize);
        void updateWorldMaskResources(vk::ImageView maskView, vk::Sampler maskSampler,
                                      vk::Buffer paramsBuffer, vk::DeviceSize paramsSize);

        void dispatch(vk::CommandBuffer cmd,
                      uint32_t imageIndex,
                      uint32_t viewMode,
                      float screenWidth,
                      float screenHeight,
                      float lodBias = 1.0f,
                      float errorThreshold = 2.0f,
                      float textureScale = 0.1f);

        TerrainCullingStats readStats();

        vk::DescriptorSetLayout getTerrainDataLayout() const { return terrainDataLayout; }
        vk::DescriptorSet getTerrainDataDescriptorSet() const { return terrainDataDescriptorSet; }
        // VK-1209: exposed for the RVT baker (set 0 = weightmap+layers).
        vk::DescriptorSetLayout getWeightMapLayout() const { return weightMapLayout; }
        vk::DescriptorSet getWeightMapDescriptorSet() const { return weightMapDescriptorSet; }
        vk::DescriptorSet getTerrainMeshletDescriptorSet() const { return terrainMeshletDescriptorSet; }
        vk::DescriptorSet getTerrainVertexDescriptorSet() const { return terrainVertexDescriptorSet; }
        vk::DescriptorSetLayout getCachedMeshletLayout() const { return cachedMeshletLayout; }
        vk::DescriptorSetLayout getCachedVertexLayout() const { return cachedVertexLayout; }
        uint32_t getCurrentTileCount() const { return currentTileCount; }

        void setFrustumCullingEnabled(bool enabled) { frustumCullingEnabled = enabled; }
        void setMeshletCullingEnabled(bool enabled) { meshletCullingEnabled = enabled; }
        void setMeshletOcclusionCullingEnabled(bool enabled) { meshletOcclusionCullingEnabled = enabled; }
        void setHiZMipLevels(uint32_t levels) { hiZMipLevels = levels; }
        void setTerrainMaxDrawDistSq(float distSq) { terrainMaxDrawDistSq = distSq; }
        void setBrushOverlay(const glm::vec3& worldPos, float worldRadius, float falloff, float shape)
        {
            brushWorldPos = glm::vec2(worldPos.x, worldPos.z);
            brushWorldY = worldPos.y;
            brushWorldRadius = worldRadius;
            brushFalloff = falloff;
            brushShape = shape;
        }

        void setStampOverlay(vk::Buffer buffer, uint32_t width, uint32_t height, float rotation)
        {
            stampOverlayBuffer = buffer;
            stampOverlayWidth = width;
            stampOverlayHeight = height;
            stampOverlayRotation = rotation;
            stampOverlayDirty = true;
        }

        void clearStampOverlay()
        {
            stampOverlayBuffer = nullptr;
            stampOverlayWidth = 0;
            stampOverlayHeight = 0;
            stampOverlayRotation = 0.0f;
            stampOverlayDirty = true;
        }

        void setStampRotation(float rotation)
        {
            stampOverlayRotation = rotation;
        }

        void setViewProjection(const glm::mat4& viewProj)
        {
            viewProjection = viewProj;
        }

        const glm::mat4& getViewProjection() const { return viewProjection; }

        // Caustic integration (Set 12)
        void setCausticEnabled(bool enabled, vk::DescriptorSetLayout layout = nullptr)
        {
            causticEnabled = enabled;
            cachedCausticLayout = layout;
        }
        bool isCausticEnabled() const { return causticEnabled; }
        void updateCausticDescriptor(vk::DescriptorSet causticDescSet)
        {
            causticDescriptorSet = causticDescSet;
            if (!causticDescSet)
                causticEnabled = false;
        }
        vk::DescriptorSet getCausticDescriptorSet() const { return causticDescriptorSet; }

    private:
        bool frustumCullingEnabled = true;
        bool meshletCullingEnabled = true;
        bool meshletOcclusionCullingEnabled = false;
        uint32_t hiZMipLevels = 0;

        void createEmptyDescriptorSet();
        void createRVTSampleDescriptor();
        void createWeightMapDescriptor();
        void createTerrainLayerBuffer();
        void createTileDataBuffer();
        void createStatsBuffer();
        void createTerrainDataDescriptor();
        void createTerrainGraphicsPipeline(vk::DescriptorSetLayout iblLayout,
                                           vk::DescriptorSetLayout bindlessTextureLayout,
                                           vk::DescriptorSetLayout meshletDataLayout,
                                           vk::DescriptorSetLayout vertexDataLayout,
                                           vk::DescriptorSetLayout lightDataLayout,
                                           vk::DescriptorSetLayout clusterGridLayout,
                                           vk::DescriptorSetLayout cullingOutputLayout,
                                           vk::DescriptorSetLayout shadowDataLayout,
                                           vk::DescriptorSetLayout shadowTextureLayout,
                                           const std::vector<vk::Format>& colorFormats, vk::Format depthFormat);
        bool loadTerrainShaders();
        // True when any RT shadow type is online and bound to the shared set 13. Kept in one place so
        // the shader-macro path (loadTerrainShaders) and the pipeline-layout path stay in lockstep.
        bool anyRTMaskActive() const
        {
            return (rtShadowEnabled && rtShadowMaskLayout) ||
                   (rtSpotShadowEnabled && rtSpotShadowMaskLayout) ||
                   (rtPointShadowEnabled && rtPointShadowMaskLayout);
        }
        void cleanupDescriptorResources();
        bool validateDescriptorsForDispatch() const;
        void bindDescriptorSetsInBatches(vk::CommandBuffer cmd,
                                         const vk::DescriptorSet* sets, uint32_t count) const;
        TerrainPushConstants buildTerrainPushConstants(uint32_t viewMode, float screenWidth, float screenHeight,
                                                       float lodBias, float errorThreshold, float textureScale) const;
    };
}
