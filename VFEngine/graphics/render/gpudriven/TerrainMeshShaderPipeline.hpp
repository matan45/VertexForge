#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <array>
#include "GPUDrivenTypes.hpp"

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::gpudriven
{
    class TerrainMeshBuffer;

    // Push constants for terrain mesh shader pipeline
    struct TerrainPushConstants
    {
        uint32_t tileCount;
        uint32_t viewMode;
        float screenWidth;
        float screenHeight;
        float lodBias;           // LOD quality bias (1.0 = normal)
        float errorThreshold;    // Screen-space error threshold in pixels
        float terrainTextureScale; // Scale for world-space UV tiling
        float padding;
    };

    // Terrain culling bits (same as regular mesh shader bits)
    constexpr uint32_t TERRAIN_CULL_FRUSTUM_BIT = 0x100;
    constexpr uint32_t TERRAIN_CULL_BACKFACE_BIT = 0x200;

    class TerrainMeshShaderPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<core::Shader> terrainShader;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;

        // Terrain tile data descriptor (set 6)
        vk::DescriptorSetLayout terrainDataLayout;
        vk::DescriptorPool terrainDataPool;
        vk::DescriptorSet terrainDataDescriptorSet;

        // Terrain tile data buffer
        vk::Buffer tileDataBuffer;
        vk::DeviceMemory tileDataBufferMemory;
        uint32_t maxTileCount = 4096;
        uint32_t currentTileCount = 0;

        // Stats buffer
        vk::Buffer statsBuffer;
        vk::DeviceMemory statsBufferMemory;
        TerrainCullingStats cachedStats{};

        // Command pool for immediate transfers
        vk::CommandPool transferCommandPool;

        // Cached descriptor set layouts from external sources
        vk::DescriptorSetLayout cachedIBLLayout;
        vk::DescriptorSetLayout cachedBindlessLayout;
        vk::DescriptorSetLayout cachedMeshletLayout;
        vk::DescriptorSetLayout cachedVertexLayout;
        vk::DescriptorSetLayout cachedLightDataLayout;
        vk::DescriptorSetLayout cachedClusterGridLayout;
        vk::DescriptorSetLayout cachedCullingOutputLayout;
        vk::DescriptorSetLayout cachedShadowDataLayout;
        vk::DescriptorSetLayout cachedShadowTextureLayout;

        // Empty descriptor set layout and sets for unused sets (1 and 5)
        vk::DescriptorSetLayout emptyLayout;
        vk::DescriptorPool emptyDescriptorPool;
        vk::DescriptorSet emptyDescriptorSet1;  // For Set 1
        vk::DescriptorSet emptyDescriptorSet5;  // For Set 5

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

        // Descriptor binding tracking - tracks last bound sets per command buffer
        // Reset when pipeline is bound, used to skip redundant bindings
        vk::CommandBuffer lastBoundCommandBuffer;
        std::array<vk::DescriptorSet, 12> lastBoundSets{};

        bool initialized = false;

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

        // Update terrain tile data on GPU
        void updateTileData(const std::vector<TerrainTileGPUData>& tiles);

        // Update descriptors from dedicated terrain buffer
        void updateTerrainBufferDescriptors(TerrainMeshBuffer& terrainBuffer);

        // Update IBL, bindless, light, cluster, and shadow descriptors (terrain buffer handles meshlet/vertex)
        void updateSharedDescriptors(vk::DescriptorSet iblDescSet,
                                     vk::DescriptorSet bindlessDescSet,
                                     vk::DescriptorSet lightDataDescSet,
                                     vk::DescriptorSet clusterGridDescSet,
                                     vk::DescriptorSet cullingOutputDescSet,
                                     vk::DescriptorSet shadowDataDescSet,
                                     vk::DescriptorSet shadowTextureDescSet);

        // Dispatch terrain rendering
        void dispatch(vk::CommandBuffer cmd,
                      uint32_t viewMode,
                      float screenWidth,
                      float screenHeight,
                      float lodBias = 1.0f,
                      float errorThreshold = 2.0f,
                      float textureScale = 0.1f);

        // Reset and read statistics
        void resetStats(vk::CommandBuffer cmd);
        TerrainCullingStats readStats();

        // Getters
        vk::Pipeline getPipeline() const { return graphicsPipeline; }
        vk::PipelineLayout getPipelineLayout() const { return pipelineLayout; }
        vk::DescriptorSetLayout getTerrainDataLayout() const { return terrainDataLayout; }
        vk::DescriptorSet getTerrainDataDescriptorSet() const { return terrainDataDescriptorSet; }
        uint32_t getCurrentTileCount() const { return currentTileCount; }
        bool isInitialized() const { return initialized; }

        // Culling settings
        void setFrustumCullingEnabled(bool enabled) { frustumCullingEnabled = enabled; }
        bool isFrustumCullingEnabled() const { return frustumCullingEnabled; }
        void setMeshletCullingEnabled(bool enabled) { meshletCullingEnabled = enabled; }
        bool isMeshletCullingEnabled() const { return meshletCullingEnabled; }

    private:
        bool frustumCullingEnabled = true;
        bool meshletCullingEnabled = true;

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
    };
}
