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

        vk::DescriptorSetLayout terrainDataLayout;
        vk::DescriptorPool terrainDataPool;
        vk::DescriptorSet terrainDataDescriptorSet;

        vk::Buffer tileDataBuffer;
        vk::DeviceMemory tileDataBufferMemory;
        uint32_t maxTileCount = 4096;
        uint32_t currentTileCount = 0;

        vk::Buffer statsBuffer;
        vk::DeviceMemory statsBufferMemory;
        TerrainCullingStats cachedStats{};

        vk::CommandPool transferCommandPool;

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

        void updateTileData(const std::vector<TerrainTileGPUData>& tiles);

        void updateTerrainBufferDescriptors(TerrainMeshBuffer& terrainBuffer);

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
