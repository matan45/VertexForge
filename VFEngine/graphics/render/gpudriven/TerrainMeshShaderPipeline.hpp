#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include "GPUDrivenTypes.hpp"

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::gpudriven
{
    class MeshletBuffer;
    class MergedMeshBuffer;

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

        // External descriptor sets (owned elsewhere)
        vk::DescriptorSet iblDescriptorSet;
        vk::DescriptorSet bindlessDescriptorSet;
        vk::DescriptorSet meshletDescriptorSet;
        vk::DescriptorSet vertexDescriptorSet;
        vk::DescriptorSet lightDataDescriptorSet;

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
                  vk::RenderPass renderPass);

        void cleanup();

        void recreate(vk::DescriptorSetLayout iblLayout,
                      vk::DescriptorSetLayout bindlessTextureLayout,
                      vk::DescriptorSetLayout meshletDataLayout,
                      vk::DescriptorSetLayout vertexDataLayout,
                      vk::DescriptorSetLayout lightDataLayout,
                      vk::RenderPass renderPass);

        // Update terrain tile data on GPU
        void updateTileData(const std::vector<TerrainTileGPUData>& tiles);

        // Update external descriptor sets
        void updateExternalDescriptors(vk::DescriptorSet iblDescSet,
                                       vk::DescriptorSet bindlessDescSet,
                                       vk::DescriptorSet meshletDescSet,
                                       vk::DescriptorSet vertexDescSet,
                                       vk::DescriptorSet lightDataDescSet);

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

    private:
        void createTileDataBuffer();
        void createStatsBuffer();
        void createTerrainDataDescriptor();
        void createTerrainGraphicsPipeline(vk::DescriptorSetLayout iblLayout,
                                           vk::DescriptorSetLayout bindlessTextureLayout,
                                           vk::DescriptorSetLayout meshletDataLayout,
                                           vk::DescriptorSetLayout vertexDataLayout,
                                           vk::DescriptorSetLayout lightDataLayout,
                                           vk::RenderPass renderPass);
    };
}
