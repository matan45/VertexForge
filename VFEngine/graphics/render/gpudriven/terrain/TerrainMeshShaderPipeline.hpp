#pragma once

#include "../../../core/VulkanMemoryManager.hpp"
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
        float _shadowLODRemoved;   // Reserved padding (was shadowLOD)
        uint32_t hiZMipLevels;     // Mip levels in the Hi-Z pyramid (0 = disabled)
        float _pad3;               // Align mat4 to 16-byte boundary (offset 64)
        glm::mat4 viewProjection; // CPU-precomputed view-projection (matches raycast invViewProjection)
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

        // SVT descriptor set (Set 12)
        vk::DescriptorSetLayout svtLayout;
        vk::DescriptorPool svtPool;
        vk::DescriptorSet svtDescriptorSet;
        bool svtEnabled = false;

        // Caustic descriptor set (Set 13) - owned by WaterCausticsResources
        vk::DescriptorSetLayout cachedCausticLayout;
        vk::DescriptorSet causticDescriptorSet;
        bool causticEnabled = false;

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
        float brushWorldRadius = 0.0f;
        float brushFalloff = 0.0f;
        float brushShape = 0.0f;
        float terrainMaxDrawDistSq = 0.0f;
        glm::mat4 viewProjection{1.0f};

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
                  vk::RenderPass renderPass);

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
                      vk::RenderPass renderPass);

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

        void dispatch(vk::CommandBuffer cmd,
                      uint32_t viewMode,
                      float screenWidth,
                      float screenHeight,
                      float lodBias = 1.0f,
                      float errorThreshold = 2.0f,
                      float textureScale = 0.1f);

        TerrainCullingStats readStats();

        vk::DescriptorSetLayout getTerrainDataLayout() const { return terrainDataLayout; }
        vk::DescriptorSet getTerrainDataDescriptorSet() const { return terrainDataDescriptorSet; }
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
        void setBrushOverlay(const glm::vec2& worldPos, float worldRadius, float falloff, float shape)
        {
            brushWorldPos = worldPos;
            brushWorldRadius = worldRadius;
            brushFalloff = falloff;
            brushShape = shape;
        }

        void setViewProjection(const glm::mat4& viewProj)
        {
            viewProjection = viewProj;
        }

        // Caustic integration (Set 13)
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

        // SVT integration
        void setSVTEnabled(bool enabled) { svtEnabled = enabled; }
        bool isSVTEnabled() const { return svtEnabled; }

        struct SVTCacheViews
        {
            vk::ImageView view;
            vk::Sampler sampler;
        };

        void initSVTDescriptorSet(vk::Buffer pageTableBuffer, vk::Buffer svtParamsBuffer,
                                   const SVTCacheViews caches[5]); // albedo, normal, ORM, emission, height
        void updateSVTParamsBuffer(vk::Buffer svtParamsBuffer);
        vk::DescriptorSet getSVTDescriptorSet() const { return svtDescriptorSet; }
        vk::DescriptorSetLayout getSVTLayout() const { return svtLayout; }

    private:
        bool frustumCullingEnabled = true;
        bool meshletCullingEnabled = true;
        bool meshletOcclusionCullingEnabled = false;
        uint32_t hiZMipLevels = 0;

        void createEmptyDescriptorSet();
        void createWeightMapDescriptor();
        void createSVTDescriptorLayout();
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
                                           vk::RenderPass renderPass);
        bool loadTerrainShaders();
        void cleanupDescriptorResources();
        bool validateDescriptorsForDispatch() const;
        void bindDescriptorSetsInBatches(vk::CommandBuffer cmd,
                                         const vk::DescriptorSet* sets, uint32_t count) const;
        TerrainPushConstants buildTerrainPushConstants(uint32_t viewMode, float screenWidth, float screenHeight,
                                                       float lodBias, float errorThreshold, float textureScale) const;
    };
}
